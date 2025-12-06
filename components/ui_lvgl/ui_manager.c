/**
 * @file ui_manager.c
 * @brief UI Manager implementation
 */

#include "ui_manager.h"
#include "ui_idle.h"
#include "ui_listening.h"
#include "ui_thinking.h"
#include "ui_speaking.h"
#include "ui_info_carousel.h"

// Manual display initialization (bypasses BSP display for proper SPI synchronization)
#include "display_init.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "UI_MANAGER";

// ============================================
// External declarations from app_main
// ============================================
extern lv_display_t *app_get_display(void);
extern lv_indev_t *app_get_input_dev(void);

// ============================================
// Private Variables
// ============================================

static bool s_initialized = false;
static ui_page_t s_current_page = UI_PAGE_BOOT;
static SemaphoreHandle_t s_ui_mutex = NULL;

// Task
static TaskHandle_t s_ui_task = NULL;
static volatile bool s_task_running = false;

// Task tuning
#define UI_TASK_STACK_SIZE   12288
#define UI_TASK_DELAY_MS     10

// LVGL timers
static lv_timer_t *s_carousel_timer = NULL;

// Callback
static ui_event_callback_t s_event_callback = NULL;
static void *s_callback_user_data = NULL;

// LVGL objects
static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_pages[UI_PAGE_MAX] = {NULL};

// Status bar objects
static lv_obj_t *s_status_bar = NULL;
static lv_obj_t *s_wifi_icon = NULL;
static lv_obj_t *s_battery_icon = NULL;
static lv_obj_t *s_time_label = NULL;

// Toast/notification
static lv_obj_t *s_toast_label = NULL;
static esp_timer_handle_t s_toast_timer = NULL;

// Current status
static bool s_wifi_connected = false;
static uint8_t s_battery_level = 100;

// ============================================
// Private Functions
// ============================================

/**
 * @brief Create status bar
 */
static void create_status_bar(lv_obj_t *parent)
{
    s_status_bar = lv_obj_create(parent);
    lv_obj_set_size(s_status_bar, UI_SCREEN_WIDTH, 40);
    lv_obj_align(s_status_bar, LV_ALIGN_TOP_MID, 0, 10);
    lv_obj_set_style_bg_opa(s_status_bar, LV_OPA_0, 0);
    lv_obj_set_style_border_width(s_status_bar, 0, 0);
    lv_obj_clear_flag(s_status_bar, LV_OBJ_FLAG_SCROLLABLE);

    // WiFi icon (left side)
    s_wifi_icon = lv_label_create(s_status_bar);
    lv_label_set_text(s_wifi_icon, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_color(s_wifi_icon, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(s_wifi_icon, &lv_font_montserrat_20, 0);
    lv_obj_align(s_wifi_icon, LV_ALIGN_LEFT_MID, 20, 0);

    // Time label (center)
    s_time_label = lv_label_create(s_status_bar);
    lv_label_set_text(s_time_label, "12:00");
    lv_obj_set_style_text_color(s_time_label, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(s_time_label, &lv_font_montserrat_18, 0);
    lv_obj_align(s_time_label, LV_ALIGN_CENTER, 0, 0);

    // Battery icon (right side)
    s_battery_icon = lv_label_create(s_status_bar);
    lv_label_set_text(s_battery_icon, LV_SYMBOL_BATTERY_FULL);
    lv_obj_set_style_text_color(s_battery_icon, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(s_battery_icon, &lv_font_montserrat_20, 0);
    lv_obj_align(s_battery_icon, LV_ALIGN_RIGHT_MID, -20, 0);
}

/**
 * @brief Create boot/splash screen
 */
static lv_obj_t *create_boot_page(lv_obj_t *parent)
{
    lv_obj_t *page = lv_obj_create(parent);
    lv_obj_set_size(page, UI_SCREEN_WIDTH, UI_SCREEN_HEIGHT);
    lv_obj_set_style_bg_color(page, UI_COLOR_BG, 0);
    lv_obj_set_style_border_width(page, 0, 0);
    lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(page);

    // Logo/title
    lv_obj_t *title = lv_label_create(page);
    lv_label_set_text(title, "Coze Voice");
    lv_obj_set_style_text_color(title, UI_COLOR_PRIMARY, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_32, 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -40);

    // Subtitle
    lv_obj_t *subtitle = lv_label_create(page);
    lv_label_set_text(subtitle, "AI Assistant");
    lv_obj_set_style_text_color(subtitle, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(subtitle, &lv_font_montserrat_18, 0);
    lv_obj_align(subtitle, LV_ALIGN_CENTER, 0, 10);

    // Loading spinner (LVGL 9.x API)
    lv_obj_t *spinner = lv_spinner_create(page);
    lv_obj_set_size(spinner, 60, 60);
    lv_obj_align(spinner, LV_ALIGN_CENTER, 0, 80);
    lv_spinner_set_anim_params(spinner, 1000, 60);  // 1000ms period, 60 degree arc
    lv_obj_set_style_arc_color(spinner, UI_COLOR_PRIMARY, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(spinner, UI_COLOR_TEXT_DIM, LV_PART_MAIN);

    // Status label
    lv_obj_t *status = lv_label_create(page);
    lv_label_set_text(status, "Initializing...");
    lv_obj_set_style_text_color(status, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(status, &lv_font_montserrat_14, 0);
    lv_obj_align(status, LV_ALIGN_CENTER, 0, 150);

    return page;
}

/**
 * @brief Create error page
 */
static lv_obj_t *create_error_page(lv_obj_t *parent)
{
    lv_obj_t *page = lv_obj_create(parent);
    lv_obj_set_size(page, UI_SCREEN_WIDTH, UI_SCREEN_HEIGHT);
    lv_obj_set_style_bg_color(page, UI_COLOR_BG, 0);
    lv_obj_set_style_border_width(page, 0, 0);
    lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(page);

    // Error icon
    lv_obj_t *icon = lv_label_create(page);
    lv_label_set_text(icon, LV_SYMBOL_WARNING);
    lv_obj_set_style_text_color(icon, UI_COLOR_ERROR, 0);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_32, 0);
    lv_obj_align(icon, LV_ALIGN_CENTER, 0, -40);

    // Error message
    lv_obj_t *msg = lv_label_create(page);
    lv_label_set_text(msg, "An error occurred");
    lv_obj_set_style_text_color(msg, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(msg, &lv_font_montserrat_18, 0);
    lv_obj_align(msg, LV_ALIGN_CENTER, 0, 20);
    lv_obj_set_user_data(page, msg);  // Store reference for updates

    // Retry hint
    lv_obj_t *hint = lv_label_create(page);
    lv_label_set_text(hint, "Tap to retry");
    lv_obj_set_style_text_color(hint, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);
    lv_obj_align(hint, LV_ALIGN_CENTER, 0, 60);

    return page;
}

/**
 * @brief Async handler to hide toast (runs in LVGL task context - thread safe)
 */
static void toast_hide_async_cb(void *arg)
{
    (void)arg;
    if (s_toast_label) {
        lv_obj_add_flag(s_toast_label, LV_OBJ_FLAG_HIDDEN);
    }
}

/**
 * @brief Toast timer callback - schedules LVGL operation for safe execution
 */
static void toast_timer_callback(void *arg)
{
    // Use lv_async_call to safely call LVGL API from esp_timer context
    lv_async_call(toast_hide_async_cb, NULL);
}

/**
 * @brief Create toast notification widget
 */
static void create_toast(lv_obj_t *parent)
{
    s_toast_label = lv_label_create(parent);
    lv_label_set_text(s_toast_label, "");
    lv_obj_set_style_text_color(s_toast_label, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(s_toast_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_bg_color(s_toast_label, lv_color_hex(0x333333), 0);
    lv_obj_set_style_bg_opa(s_toast_label, LV_OPA_90, 0);
    lv_obj_set_style_pad_all(s_toast_label, 10, 0);
    lv_obj_set_style_radius(s_toast_label, 10, 0);
    lv_obj_align(s_toast_label, LV_ALIGN_BOTTOM_MID, 0, -80);
    lv_obj_add_flag(s_toast_label, LV_OBJ_FLAG_HIDDEN);

    // Create timer for auto-hide
    esp_timer_create_args_t timer_args = {
        .callback = toast_timer_callback,
        .name = "toast_timer"
    };
    esp_timer_create(&timer_args, &s_toast_timer);
}

/**
 * @brief Show toast notification
 */
static void show_toast(const char *message, uint32_t duration_ms)
{
    if (s_toast_label == NULL) return;

    if (ui_manager_lock(100)) {
        lv_label_set_text(s_toast_label, message);
        lv_obj_clear_flag(s_toast_label, LV_OBJ_FLAG_HIDDEN);
        ui_manager_unlock();

        // Start timer to hide toast
        esp_timer_stop(s_toast_timer);
        esp_timer_start_once(s_toast_timer, duration_ms * 1000);
    }
}

/**
 * @brief LVGL touch event callback for gesture detection
 */
static void screen_event_cb(lv_event_t *e)
{
    if (s_event_callback == NULL) return;

    lv_event_code_t code = lv_event_get_code(e);
    ui_event_t event = UI_EVENT_NONE;

    switch (code) {
        case LV_EVENT_CLICKED:
            event = UI_EVENT_TAP;
            break;
        case LV_EVENT_LONG_PRESSED:
            event = UI_EVENT_LONG_PRESS;
            break;
        case LV_EVENT_GESTURE:
            {
                lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_active());
                switch (dir) {
                    case LV_DIR_TOP:
                        event = UI_EVENT_SWIPE_UP;
                        break;
                    case LV_DIR_BOTTOM:
                        event = UI_EVENT_SWIPE_DOWN;
                        break;
                    case LV_DIR_LEFT:
                        event = UI_EVENT_SWIPE_LEFT;
                        break;
                    case LV_DIR_RIGHT:
                        event = UI_EVENT_SWIPE_RIGHT;
                        break;
                    default:
                        break;
                }
            }
            break;
        default:
            break;
    }

    if (event != UI_EVENT_NONE) {
        s_event_callback(event, s_callback_user_data);
    }
}

static void carousel_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (s_current_page == UI_PAGE_IDLE && ui_info_carousel_is_visible()) {
        if (ui_manager_lock(50)) {
            ui_info_carousel_update();
            ui_manager_unlock();
        }
    }
}

// ============================================
// Public Functions
// ============================================

esp_err_t ui_manager_init(void)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "UI manager already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing UI manager...");

    // Create mutex
    s_ui_mutex = xSemaphoreCreateRecursiveMutex();
    if (s_ui_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create UI mutex");
        return ESP_ERR_NO_MEM;
    }

    // Get LVGL display from app_main
    lv_display_t *disp = app_get_display();
    if (disp == NULL) {
        ESP_LOGE(TAG, "Display not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    // Guard LVGL calls while the LVGL task is running
    if (!ui_manager_lock(1000)) {
        ESP_LOGE(TAG, "Failed to lock LVGL during init");
        return ESP_ERR_TIMEOUT;
    }

    // Create main screen
    s_screen = lv_display_get_screen_active(disp);
    lv_obj_set_style_bg_color(s_screen, UI_COLOR_BG, 0);

    // Add event handler for touch gestures
    lv_obj_add_event_cb(s_screen, screen_event_cb, LV_EVENT_ALL, NULL);

    // Create status bar
    create_status_bar(s_screen);

    // Create pages
    s_pages[UI_PAGE_BOOT] = create_boot_page(s_screen);
    s_pages[UI_PAGE_IDLE] = ui_idle_create(s_screen);
    s_pages[UI_PAGE_LISTENING] = ui_listening_create(s_screen);
    s_pages[UI_PAGE_THINKING] = ui_thinking_create(s_screen);
    s_pages[UI_PAGE_SPEAKING] = ui_speaking_create(s_screen);
    s_pages[UI_PAGE_ERROR] = create_error_page(s_screen);

    // Initialize info carousel on IDLE page
    if (s_pages[UI_PAGE_IDLE]) {
        ui_info_carousel_init(s_pages[UI_PAGE_IDLE]);
    }

    // Hide all pages except boot
    for (int i = 0; i < UI_PAGE_MAX; i++) {
        if (s_pages[i] && i != UI_PAGE_BOOT) {
            lv_obj_add_flag(s_pages[i], LV_OBJ_FLAG_HIDDEN);
        }
    }

    // Create toast notification
    create_toast(s_screen);

    s_initialized = true;
    s_current_page = UI_PAGE_BOOT;

    // Carousel update every 2 seconds (reduced frequency to minimize flickering)
    s_carousel_timer = lv_timer_create(carousel_timer_cb, 2000, NULL);
    if (!s_carousel_timer) {
        ESP_LOGW(TAG, "Failed to create carousel timer");
    }

    ui_manager_unlock();

    ESP_LOGI(TAG, "UI manager initialized");
    return ESP_OK;
}

esp_err_t ui_manager_deinit(void)
{
    if (!s_initialized) {
        return ESP_OK;
    }

    ui_manager_stop_task();

    // Destroy carousel
    ui_info_carousel_deinit();

    // Destroy pages
    ui_idle_destroy();
    ui_listening_destroy();
    ui_thinking_destroy();
    ui_speaking_destroy();

    // Delete toast timer
    if (s_toast_timer) {
        esp_timer_delete(s_toast_timer);
        s_toast_timer = NULL;
    }

    if (s_ui_mutex) {
        vSemaphoreDelete(s_ui_mutex);
        s_ui_mutex = NULL;
    }

    s_initialized = false;
    ESP_LOGI(TAG, "UI manager deinitialized");
    return ESP_OK;
}

esp_err_t ui_manager_start_task(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    // Start LVGL task from manual display init (bypasses BSP)
    esp_err_t ret = display_start_task();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start display task: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "LVGL task started (manual display init)");
    return ESP_OK;
}

esp_err_t ui_manager_stop_task(void)
{
    return ESP_OK;
}

esp_err_t ui_manager_set_page(ui_page_t page)
{
    if (!s_initialized || page >= UI_PAGE_MAX) {
        return ESP_ERR_INVALID_ARG;
    }

    // 🛡️ Second layer of defense: Check if display is available
    if (app_get_display() == NULL) {
        ESP_LOGW(TAG, "Display not available, skipping page transition to %s",
                 ui_manager_page_to_string(page));
        return ESP_ERR_INVALID_STATE;
    }

    if (page == s_current_page) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Page transition: %s -> %s",
             ui_manager_page_to_string(s_current_page),
             ui_manager_page_to_string(page));

    if (!ui_manager_lock(1000)) {
        return ESP_ERR_TIMEOUT;
    }

    // Exit current page
    switch (s_current_page) {
        case UI_PAGE_IDLE:
            ui_info_carousel_hide();
            ui_idle_exit();
            break;
        case UI_PAGE_LISTENING:
            ui_listening_exit();
            break;
        case UI_PAGE_THINKING:
            ui_thinking_exit();
            break;
        case UI_PAGE_SPEAKING:
            ui_speaking_exit();
            break;
        default:
            break;
    }

    // Hide current page
    if (s_pages[s_current_page]) {
        lv_obj_add_flag(s_pages[s_current_page], LV_OBJ_FLAG_HIDDEN);
    }

    // Show new page
    if (s_pages[page]) {
        lv_obj_clear_flag(s_pages[page], LV_OBJ_FLAG_HIDDEN);
    }

    // Force full screen invalidation to prevent remnants during transition
    // This ensures the entire screen is redrawn with new page content
    lv_obj_invalidate(s_screen);

    // Enter new page
    switch (page) {
        case UI_PAGE_IDLE:
            ui_idle_enter();
            ui_info_carousel_show();
            ui_info_carousel_update();
            break;
        case UI_PAGE_LISTENING:
            ui_listening_enter();
            break;
        case UI_PAGE_THINKING:
            ui_thinking_enter();
            break;
        case UI_PAGE_SPEAKING:
            ui_speaking_enter();
            break;
        default:
            break;
    }

    s_current_page = page;
    ui_manager_unlock();

    return ESP_OK;
}

ui_page_t ui_manager_get_page(void)
{
    return s_current_page;
}

void ui_manager_show_boot_screen(void)
{
    ui_manager_set_page(UI_PAGE_BOOT);
}

void ui_manager_show_status(const char *message, bool success)
{
    show_toast(message, 3000);
}

void ui_manager_show_error(const char *message)
{
    if (ui_manager_lock(100)) {
        if (s_pages[UI_PAGE_ERROR]) {
            lv_obj_t *msg_label = lv_obj_get_user_data(s_pages[UI_PAGE_ERROR]);
            if (msg_label) {
                lv_label_set_text(msg_label, message);
            }
        }
        ui_manager_unlock();
    }
    ui_manager_set_page(UI_PAGE_ERROR);
}

void ui_manager_update_transcript(const char *text, bool is_user)
{
    if (app_get_display() == NULL) {
        return;
    }

    if (!ui_manager_lock(50)) return;

    if (is_user && s_current_page == UI_PAGE_LISTENING) {
        ui_listening_update_text(text);
    } else if (!is_user && s_current_page == UI_PAGE_SPEAKING) {
        ui_speaking_append_text(text);
    }

    ui_manager_unlock();
}

void ui_manager_clear_transcript(void)
{
    if (app_get_display() == NULL) {
        return;
    }

    if (!ui_manager_lock(50)) return;

    ui_listening_clear_text();
    ui_speaking_clear_text();

    ui_manager_unlock();
}

void ui_manager_update_audio_level(uint8_t level)
{
    // Skip if UI not initialized or display not available
    if (!s_initialized || app_get_display() == NULL) {
        return;
    }

    if (!ui_manager_lock(10)) return;

    if (s_current_page == UI_PAGE_LISTENING) {
        ui_listening_update_level(level);
    } else if (s_current_page == UI_PAGE_SPEAKING) {
        ui_speaking_update_level(level);
    }

    ui_manager_unlock();
}

void ui_manager_update_wifi_status(bool connected, int rssi)
{
    s_wifi_connected = connected;

    if (!ui_manager_lock(50)) return;

    if (s_wifi_icon) {
        if (connected) {
            lv_obj_set_style_text_color(s_wifi_icon, UI_COLOR_SECONDARY, 0);
        } else {
            lv_obj_set_style_text_color(s_wifi_icon, UI_COLOR_TEXT_DIM, 0);
        }
    }

    ui_manager_unlock();
}

void ui_manager_update_battery(uint8_t level, bool charging)
{
    s_battery_level = level;

    if (!ui_manager_lock(50)) return;

    if (s_battery_icon) {
        const char *icon;
        if (charging) {
            icon = LV_SYMBOL_CHARGE;
        } else if (level > 75) {
            icon = LV_SYMBOL_BATTERY_FULL;
        } else if (level > 50) {
            icon = LV_SYMBOL_BATTERY_3;
        } else if (level > 25) {
            icon = LV_SYMBOL_BATTERY_2;
        } else if (level > 10) {
            icon = LV_SYMBOL_BATTERY_1;
        } else {
            icon = LV_SYMBOL_BATTERY_EMPTY;
        }
        lv_label_set_text(s_battery_icon, icon);

        // Color based on level
        if (level <= 20 && !charging) {
            lv_obj_set_style_text_color(s_battery_icon, UI_COLOR_ERROR, 0);
        } else {
            lv_obj_set_style_text_color(s_battery_icon, UI_COLOR_TEXT_DIM, 0);
        }
    }

    ui_manager_unlock();
}

esp_err_t ui_manager_register_callback(ui_event_callback_t callback, void *user_data)
{
    s_event_callback = callback;
    s_callback_user_data = user_data;
    return ESP_OK;
}

bool ui_manager_lock(int timeout_ms)
{
    // Use manual display lock (bypasses BSP for proper SPI synchronization)
    return display_lock(timeout_ms);
}

void ui_manager_unlock(void)
{
    // Use manual display unlock
    display_unlock();
}

void ui_manager_refresh(void)
{
    if (ui_manager_lock(50)) {
        lv_refr_now(NULL);
        ui_manager_unlock();
    }
}

const char *ui_manager_page_to_string(ui_page_t page)
{
    switch (page) {
        case UI_PAGE_BOOT: return "BOOT";
        case UI_PAGE_IDLE: return "IDLE";
        case UI_PAGE_LISTENING: return "LISTENING";
        case UI_PAGE_THINKING: return "THINKING";
        case UI_PAGE_SPEAKING: return "SPEAKING";
        case UI_PAGE_ERROR: return "ERROR";
        case UI_PAGE_SETTINGS: return "SETTINGS";
        default: return "UNKNOWN";
    }
}
