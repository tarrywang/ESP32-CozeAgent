/**
 * @file ui_info_carousel.c
 * @brief Information carousel implementation using LVGL tileview
 */

#include "ui_info_carousel.h"
#include "ui_manager.h"
#include "system_info.h"
#include "esp_log.h"
#include <stdio.h>

static const char *TAG = "ui_carousel";

// ============================================
// Configuration
// ============================================

#define AUTO_ROTATE_INTERVAL_MS     5000    // 5 seconds
#define CAROUSEL_WIDTH              400     // Carousel width
#define CAROUSEL_HEIGHT             200     // Carousel height
#define INDICATOR_SIZE              8       // Page indicator dot size
#define INDICATOR_SPACING           6       // Spacing between dots

// ============================================
// Private Variables
// ============================================

static bool s_initialized = false;
static bool s_visible = false;
static lv_obj_t *s_tileview = NULL;
static lv_obj_t *s_tiles[CAROUSEL_PAGE_MAX] = {0};
static lv_obj_t *s_indicator_container = NULL;
static lv_obj_t *s_indicators[CAROUSEL_PAGE_MAX] = {0};
static lv_timer_t *s_auto_rotate_timer = NULL;
static carousel_page_t s_current_page = CAROUSEL_PAGE_WIFI;

// Tile content labels
static lv_obj_t *s_wifi_icon = NULL;
static lv_obj_t *s_wifi_ssid_label = NULL;
static lv_obj_t *s_wifi_rssi_label = NULL;

static lv_obj_t *s_batt_icon = NULL;
static lv_obj_t *s_batt_percent_label = NULL;
static lv_obj_t *s_batt_status_label = NULL;

static lv_obj_t *s_mem_icon = NULL;
static lv_obj_t *s_mem_usage_label = NULL;
static lv_obj_t *s_mem_detail_label = NULL;

static lv_obj_t *s_temp_icon = NULL;
static lv_obj_t *s_temp_value_label = NULL;
static lv_obj_t *s_temp_status_label = NULL;

static lv_obj_t *s_gps_icon = NULL;
static lv_obj_t *s_gps_status_label = NULL;
static lv_obj_t *s_gps_coord_label = NULL;

static lv_obj_t *s_time_label = NULL;
static lv_obj_t *s_date_label = NULL;
static lv_obj_t *s_weekday_label = NULL;

// ============================================
// Forward Declarations
// ============================================

static void create_wifi_tile(lv_obj_t *tile);
static void create_battery_tile(lv_obj_t *tile);
static void create_memory_tile(lv_obj_t *tile);
static void create_temperature_tile(lv_obj_t *tile);
static void create_gps_tile(lv_obj_t *tile);
static void create_datetime_tile(lv_obj_t *tile);
static void create_page_indicator(lv_obj_t *parent);
static void update_page_indicator(carousel_page_t page);
static void auto_rotate_timer_cb(lv_timer_t *timer);
static void tileview_event_cb(lv_event_t *e);

// ============================================
// Tile Creation Functions
// ============================================

static void create_tile_base_style(lv_obj_t *tile)
{
    lv_obj_set_style_bg_color(tile, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_style_bg_opa(tile, LV_OPA_90, 0);
    lv_obj_set_style_border_width(tile, 0, 0);
    lv_obj_set_style_pad_all(tile, 20, 0);
    lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
}

static void create_wifi_tile(lv_obj_t *tile)
{
    create_tile_base_style(tile);

    // WiFi icon
    s_wifi_icon = lv_label_create(tile);
    lv_label_set_text(s_wifi_icon, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_font(s_wifi_icon, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(s_wifi_icon, UI_COLOR_PRIMARY, 0);
    lv_obj_align(s_wifi_icon, LV_ALIGN_TOP_MID, 0, 10);

    // SSID
    s_wifi_ssid_label = lv_label_create(tile);
    lv_label_set_text(s_wifi_ssid_label, "Not Connected");
    lv_obj_set_style_text_font(s_wifi_ssid_label, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(s_wifi_ssid_label, UI_COLOR_TEXT, 0);
    lv_obj_align(s_wifi_ssid_label, LV_ALIGN_CENTER, 0, 10);

    // RSSI
    s_wifi_rssi_label = lv_label_create(tile);
    lv_label_set_text(s_wifi_rssi_label, "Signal: --");
    lv_obj_set_style_text_font(s_wifi_rssi_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_wifi_rssi_label, UI_COLOR_TEXT_DIM, 0);
    lv_obj_align(s_wifi_rssi_label, LV_ALIGN_CENTER, 0, 40);
}

static void create_battery_tile(lv_obj_t *tile)
{
    create_tile_base_style(tile);

    // Battery icon
    s_batt_icon = lv_label_create(tile);
    lv_label_set_text(s_batt_icon, LV_SYMBOL_BATTERY_FULL);
    lv_obj_set_style_text_font(s_batt_icon, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(s_batt_icon, UI_COLOR_SECONDARY, 0);
    lv_obj_align(s_batt_icon, LV_ALIGN_TOP_MID, 0, 10);

    // Percentage
    s_batt_percent_label = lv_label_create(tile);
    lv_label_set_text(s_batt_percent_label, "--%");
    lv_obj_set_style_text_font(s_batt_percent_label, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(s_batt_percent_label, UI_COLOR_TEXT, 0);
    lv_obj_align(s_batt_percent_label, LV_ALIGN_CENTER, 0, 10);

    // Status
    s_batt_status_label = lv_label_create(tile);
    lv_label_set_text(s_batt_status_label, "Unknown");
    lv_obj_set_style_text_font(s_batt_status_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_batt_status_label, UI_COLOR_TEXT_DIM, 0);
    lv_obj_align(s_batt_status_label, LV_ALIGN_CENTER, 0, 45);
}

static void create_memory_tile(lv_obj_t *tile)
{
    create_tile_base_style(tile);

    // Memory icon (using settings icon as placeholder)
    s_mem_icon = lv_label_create(tile);
    lv_label_set_text(s_mem_icon, LV_SYMBOL_SD_CARD);
    lv_obj_set_style_text_font(s_mem_icon, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(s_mem_icon, UI_COLOR_ACCENT, 0);
    lv_obj_align(s_mem_icon, LV_ALIGN_TOP_MID, 0, 10);

    // Usage percentage
    s_mem_usage_label = lv_label_create(tile);
    lv_label_set_text(s_mem_usage_label, "--%");
    lv_obj_set_style_text_font(s_mem_usage_label, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(s_mem_usage_label, UI_COLOR_TEXT, 0);
    lv_obj_align(s_mem_usage_label, LV_ALIGN_CENTER, 0, 10);

    // Detail
    s_mem_detail_label = lv_label_create(tile);
    lv_label_set_text(s_mem_detail_label, "Free: -- KB");
    lv_obj_set_style_text_font(s_mem_detail_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_mem_detail_label, UI_COLOR_TEXT_DIM, 0);
    lv_obj_align(s_mem_detail_label, LV_ALIGN_CENTER, 0, 45);
}

static void create_temperature_tile(lv_obj_t *tile)
{
    create_tile_base_style(tile);

    // Temperature icon (using warning as placeholder for thermometer)
    s_temp_icon = lv_label_create(tile);
    lv_label_set_text(s_temp_icon, LV_SYMBOL_WARNING);  // Replace with custom icon if available
    lv_obj_set_style_text_font(s_temp_icon, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(s_temp_icon, UI_COLOR_THINKING, 0);
    lv_obj_align(s_temp_icon, LV_ALIGN_TOP_MID, 0, 10);

    // Temperature value
    s_temp_value_label = lv_label_create(tile);
    lv_label_set_text(s_temp_value_label, "--.-°C");
    lv_obj_set_style_text_font(s_temp_value_label, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(s_temp_value_label, UI_COLOR_TEXT, 0);
    lv_obj_align(s_temp_value_label, LV_ALIGN_CENTER, 0, 10);

    // Status
    s_temp_status_label = lv_label_create(tile);
    lv_label_set_text(s_temp_status_label, "PMU Temperature");
    lv_obj_set_style_text_font(s_temp_status_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_temp_status_label, UI_COLOR_TEXT_DIM, 0);
    lv_obj_align(s_temp_status_label, LV_ALIGN_CENTER, 0, 45);
}

static void create_gps_tile(lv_obj_t *tile)
{
    create_tile_base_style(tile);

    // GPS icon
    s_gps_icon = lv_label_create(tile);
    lv_label_set_text(s_gps_icon, LV_SYMBOL_GPS);
    lv_obj_set_style_text_font(s_gps_icon, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(s_gps_icon, UI_COLOR_ERROR, 0);
    lv_obj_align(s_gps_icon, LV_ALIGN_TOP_MID, 0, 10);

    // Status
    s_gps_status_label = lv_label_create(tile);
    lv_label_set_text(s_gps_status_label, "Not Available");
    lv_obj_set_style_text_font(s_gps_status_label, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(s_gps_status_label, UI_COLOR_TEXT, 0);
    lv_obj_align(s_gps_status_label, LV_ALIGN_CENTER, 0, 10);

    // Coordinates
    s_gps_coord_label = lv_label_create(tile);
    lv_label_set_text(s_gps_coord_label, "");
    lv_obj_set_style_text_font(s_gps_coord_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_gps_coord_label, UI_COLOR_TEXT_DIM, 0);
    lv_obj_align(s_gps_coord_label, LV_ALIGN_CENTER, 0, 45);
}

static void create_datetime_tile(lv_obj_t *tile)
{
    create_tile_base_style(tile);

    // Time (large)
    s_time_label = lv_label_create(tile);
    lv_label_set_text(s_time_label, "--:--:--");
    lv_obj_set_style_text_font(s_time_label, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(s_time_label, UI_COLOR_TEXT, 0);
    lv_obj_align(s_time_label, LV_ALIGN_CENTER, 0, -10);

    // Date
    s_date_label = lv_label_create(tile);
    lv_label_set_text(s_date_label, "----/--/--");
    lv_obj_set_style_text_font(s_date_label, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(s_date_label, UI_COLOR_TEXT_DIM, 0);
    lv_obj_align(s_date_label, LV_ALIGN_CENTER, 0, 30);

    // Weekday
    s_weekday_label = lv_label_create(tile);
    lv_label_set_text(s_weekday_label, "");
    lv_obj_set_style_text_font(s_weekday_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_weekday_label, UI_COLOR_PRIMARY, 0);
    lv_obj_align(s_weekday_label, LV_ALIGN_CENTER, 0, 55);
}

// ============================================
// Page Indicator
// ============================================

static void create_page_indicator(lv_obj_t *parent)
{
    s_indicator_container = lv_obj_create(parent);
    lv_obj_set_size(s_indicator_container,
                    (INDICATOR_SIZE + INDICATOR_SPACING) * CAROUSEL_PAGE_MAX,
                    INDICATOR_SIZE + 4);
    lv_obj_set_style_bg_opa(s_indicator_container, LV_OPA_0, 0);
    lv_obj_set_style_border_width(s_indicator_container, 0, 0);
    lv_obj_set_style_pad_all(s_indicator_container, 0, 0);
    lv_obj_align(s_indicator_container, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_clear_flag(s_indicator_container, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < CAROUSEL_PAGE_MAX; i++) {
        s_indicators[i] = lv_obj_create(s_indicator_container);
        lv_obj_set_size(s_indicators[i], INDICATOR_SIZE, INDICATOR_SIZE);
        lv_obj_set_style_radius(s_indicators[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(s_indicators[i], 0, 0);

        if (i == 0) {
            lv_obj_set_style_bg_color(s_indicators[i], UI_COLOR_PRIMARY, 0);
            lv_obj_set_style_bg_opa(s_indicators[i], LV_OPA_COVER, 0);
        } else {
            lv_obj_set_style_bg_color(s_indicators[i], UI_COLOR_TEXT_DIM, 0);
            lv_obj_set_style_bg_opa(s_indicators[i], LV_OPA_50, 0);
        }

        lv_obj_set_pos(s_indicators[i],
                       i * (INDICATOR_SIZE + INDICATOR_SPACING), 0);
    }
}

/**
 * @brief Indicator size animation callback
 */
static void indicator_size_anim_cb(void *var, int32_t value)
{
    lv_obj_t *obj = (lv_obj_t *)var;
    if (obj) {
        lv_obj_set_size(obj, value, value);
    }
}

static void update_page_indicator(carousel_page_t page)
{
    for (int i = 0; i < CAROUSEL_PAGE_MAX; i++) {
        if (s_indicators[i]) {
            if (i == (int)page) {
                // Animate active indicator
                lv_anim_t a;
                lv_anim_init(&a);
                lv_anim_set_var(&a, s_indicators[i]);
                lv_anim_set_values(&a, INDICATOR_SIZE, INDICATOR_SIZE + 4);
                lv_anim_set_time(&a, 150);
                lv_anim_set_exec_cb(&a, indicator_size_anim_cb);
                lv_anim_start(&a);

                lv_obj_set_style_bg_color(s_indicators[i], UI_COLOR_PRIMARY, 0);
                lv_obj_set_style_bg_opa(s_indicators[i], LV_OPA_COVER, 0);
            } else {
                // Shrink inactive indicators
                lv_obj_set_size(s_indicators[i], INDICATOR_SIZE, INDICATOR_SIZE);
                lv_obj_set_style_bg_color(s_indicators[i], UI_COLOR_TEXT_DIM, 0);
                lv_obj_set_style_bg_opa(s_indicators[i], LV_OPA_50, 0);
            }
        }
    }
}

// ============================================
// Event Callbacks
// ============================================

/**
 * @brief Auto-rotate timer callback - runs in LVGL task context (thread-safe)
 */
static void auto_rotate_timer_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);

    if (!s_visible || !s_tileview) {
        return;
    }

    ui_info_carousel_next_page(true);
}

static void tileview_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_VALUE_CHANGED) {
        lv_obj_t *active_tile = lv_tileview_get_tile_active(s_tileview);

        for (int i = 0; i < CAROUSEL_PAGE_MAX; i++) {
            if (s_tiles[i] == active_tile) {
                s_current_page = (carousel_page_t)i;
                update_page_indicator(s_current_page);

                // Reset auto-rotate timer on manual interaction
                if (s_auto_rotate_timer) {
                    lv_timer_reset(s_auto_rotate_timer);
                }
                break;
            }
        }
    }
}

// ============================================
// Public Functions
// ============================================

esp_err_t ui_info_carousel_init(lv_obj_t *parent)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "Carousel already initialized");
        return ESP_OK;
    }

    if (parent == NULL) {
        ESP_LOGE(TAG, "Parent is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Initializing info carousel");

    // Create tileview
    s_tileview = lv_tileview_create(parent);
    lv_obj_set_size(s_tileview, CAROUSEL_WIDTH, CAROUSEL_HEIGHT);
    lv_obj_align(s_tileview, LV_ALIGN_BOTTOM_MID, 0, -60);
    lv_obj_set_style_bg_opa(s_tileview, LV_OPA_0, 0);
    lv_obj_set_style_radius(s_tileview, 20, 0);
    lv_obj_add_event_cb(s_tileview, tileview_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    // Create tiles (horizontal arrangement)
    for (int i = 0; i < CAROUSEL_PAGE_MAX; i++) {
        s_tiles[i] = lv_tileview_add_tile(s_tileview, i, 0, LV_DIR_HOR);
    }

    // Create content for each tile
    create_wifi_tile(s_tiles[CAROUSEL_PAGE_WIFI]);
    create_battery_tile(s_tiles[CAROUSEL_PAGE_BATTERY]);
    create_memory_tile(s_tiles[CAROUSEL_PAGE_MEMORY]);
    create_temperature_tile(s_tiles[CAROUSEL_PAGE_TEMPERATURE]);
    create_gps_tile(s_tiles[CAROUSEL_PAGE_GPS]);
    create_datetime_tile(s_tiles[CAROUSEL_PAGE_DATETIME]);

    // Create page indicator
    create_page_indicator(parent);

    // Create auto-rotate timer using LVGL timer (runs in LVGL task context - thread safe)
    s_auto_rotate_timer = lv_timer_create(auto_rotate_timer_cb, AUTO_ROTATE_INTERVAL_MS, NULL);
    if (s_auto_rotate_timer == NULL) {
        ESP_LOGE(TAG, "Failed to create LVGL timer");
        return ESP_ERR_NO_MEM;
    }
    // Initially pause the timer (will be started when carousel is shown)
    lv_timer_pause(s_auto_rotate_timer);

    // Initially hidden
    lv_obj_add_flag(s_tileview, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_indicator_container, LV_OBJ_FLAG_HIDDEN);

    s_initialized = true;
    s_visible = false;
    s_current_page = CAROUSEL_PAGE_WIFI;

    ESP_LOGI(TAG, "Info carousel initialized");
    return ESP_OK;
}

esp_err_t ui_info_carousel_deinit(void)
{
    if (!s_initialized) {
        return ESP_OK;
    }

    ui_info_carousel_stop_auto_rotate();

    if (s_auto_rotate_timer) {
        lv_timer_delete(s_auto_rotate_timer);
        s_auto_rotate_timer = NULL;
    }

    // LVGL objects will be deleted when parent is deleted

    s_initialized = false;
    s_visible = false;
    s_tileview = NULL;
    s_indicator_container = NULL;

    for (int i = 0; i < CAROUSEL_PAGE_MAX; i++) {
        s_tiles[i] = NULL;
        s_indicators[i] = NULL;
    }

    ESP_LOGI(TAG, "Info carousel deinitialized");
    return ESP_OK;
}

/**
 * @brief Fade animation callback
 */
static void fade_anim_cb(void *var, int32_t value)
{
    lv_obj_t *obj = (lv_obj_t *)var;
    if (obj) {
        lv_obj_set_style_opa(obj, (lv_opa_t)value, 0);
    }
}

void ui_info_carousel_show(void)
{
    if (!s_initialized || s_visible) {
        return;
    }

    if (s_tileview) {
        lv_obj_clear_flag(s_tileview, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_opa(s_tileview, LV_OPA_0, 0);

        // Fade in animation
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, s_tileview);
        lv_anim_set_values(&a, LV_OPA_0, LV_OPA_COVER);
        lv_anim_set_time(&a, 300);
        lv_anim_set_exec_cb(&a, fade_anim_cb);
        lv_anim_start(&a);
    }
    if (s_indicator_container) {
        lv_obj_clear_flag(s_indicator_container, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_opa(s_indicator_container, LV_OPA_0, 0);

        // Fade in animation for indicators (with slight delay)
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, s_indicator_container);
        lv_anim_set_values(&a, LV_OPA_0, LV_OPA_COVER);
        lv_anim_set_time(&a, 300);
        lv_anim_set_delay(&a, 100);
        lv_anim_set_exec_cb(&a, fade_anim_cb);
        lv_anim_start(&a);
    }

    s_visible = true;
    ui_info_carousel_start_auto_rotate();

    ESP_LOGI(TAG, "Carousel shown");
}

/**
 * @brief Hide animation ready callback - actually hide objects after fade out
 */
static void hide_anim_ready_cb(lv_anim_t *a)
{
    lv_obj_t *obj = (lv_obj_t *)a->var;
    if (obj) {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}

void ui_info_carousel_hide(void)
{
    if (!s_initialized || !s_visible) {
        return;
    }

    ui_info_carousel_stop_auto_rotate();
    s_visible = false;

    // Fade out animations
    if (s_indicator_container) {
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, s_indicator_container);
        lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_0);
        lv_anim_set_time(&a, 200);
        lv_anim_set_exec_cb(&a, fade_anim_cb);
        lv_anim_set_ready_cb(&a, hide_anim_ready_cb);
        lv_anim_start(&a);
    }
    if (s_tileview) {
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, s_tileview);
        lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_0);
        lv_anim_set_time(&a, 200);
        lv_anim_set_delay(&a, 50);
        lv_anim_set_exec_cb(&a, fade_anim_cb);
        lv_anim_set_ready_cb(&a, hide_anim_ready_cb);
        lv_anim_start(&a);
    }

    ESP_LOGI(TAG, "Carousel hidden");
}

bool ui_info_carousel_is_visible(void)
{
    return s_visible;
}

void ui_info_carousel_update(void)
{
    if (!s_initialized) {
        return;
    }

    system_info_t info;
    if (system_info_get(&info) != ESP_OK) {
        return;
    }

    char buf[64];

    // Update WiFi tile
    if (s_wifi_ssid_label) {
        if (info.wifi_connected) {
            lv_label_set_text(s_wifi_ssid_label, info.wifi_ssid[0] ? info.wifi_ssid : "Connected");
            lv_obj_set_style_text_color(s_wifi_icon, UI_COLOR_SECONDARY, 0);

            snprintf(buf, sizeof(buf), "Signal: %s (%d dBm)",
                     system_info_rssi_to_string(info.wifi_rssi), info.wifi_rssi);
            lv_label_set_text(s_wifi_rssi_label, buf);
        } else {
            lv_label_set_text(s_wifi_ssid_label, "Not Connected");
            lv_label_set_text(s_wifi_rssi_label, "Signal: --");
            lv_obj_set_style_text_color(s_wifi_icon, UI_COLOR_ERROR, 0);
        }
    }

    // Update Battery tile
    if (s_batt_percent_label) {
        snprintf(buf, sizeof(buf), "%d%%", info.battery_percent);
        lv_label_set_text(s_batt_percent_label, buf);

        const char *status = system_info_battery_status(info.battery_percent, info.battery_charging);
        lv_label_set_text(s_batt_status_label, status);

        // Update icon color based on level
        if (info.battery_charging) {
            lv_obj_set_style_text_color(s_batt_icon, UI_COLOR_SECONDARY, 0);
            lv_label_set_text(s_batt_icon, LV_SYMBOL_CHARGE);
        } else if (info.battery_percent < 20) {
            lv_obj_set_style_text_color(s_batt_icon, UI_COLOR_ERROR, 0);
            lv_label_set_text(s_batt_icon, LV_SYMBOL_BATTERY_EMPTY);
        } else if (info.battery_percent < 50) {
            lv_obj_set_style_text_color(s_batt_icon, UI_COLOR_THINKING, 0);
            lv_label_set_text(s_batt_icon, LV_SYMBOL_BATTERY_2);
        } else if (info.battery_percent < 80) {
            lv_obj_set_style_text_color(s_batt_icon, UI_COLOR_SECONDARY, 0);
            lv_label_set_text(s_batt_icon, LV_SYMBOL_BATTERY_3);
        } else {
            lv_obj_set_style_text_color(s_batt_icon, UI_COLOR_SECONDARY, 0);
            lv_label_set_text(s_batt_icon, LV_SYMBOL_BATTERY_FULL);
        }
    }

    // Update Memory tile
    if (s_mem_usage_label) {
        snprintf(buf, sizeof(buf), "%d%%", info.heap_usage_percent);
        lv_label_set_text(s_mem_usage_label, buf);

        snprintf(buf, sizeof(buf), "Free: %lu KB", (unsigned long)(info.free_heap / 1024));
        lv_label_set_text(s_mem_detail_label, buf);

        // Update color based on usage
        if (info.heap_usage_percent > 90) {
            lv_obj_set_style_text_color(s_mem_icon, UI_COLOR_ERROR, 0);
        } else if (info.heap_usage_percent > 70) {
            lv_obj_set_style_text_color(s_mem_icon, UI_COLOR_THINKING, 0);
        } else {
            lv_obj_set_style_text_color(s_mem_icon, UI_COLOR_ACCENT, 0);
        }
    }

    // Update Temperature tile
    if (s_temp_value_label) {
        snprintf(buf, sizeof(buf), "%.1f°C", info.temperature_c);
        lv_label_set_text(s_temp_value_label, buf);

        // Update color based on temperature
        if (info.temperature_c > 60) {
            lv_obj_set_style_text_color(s_temp_icon, UI_COLOR_ERROR, 0);
            lv_label_set_text(s_temp_status_label, "High Temperature!");
        } else if (info.temperature_c > 45) {
            lv_obj_set_style_text_color(s_temp_icon, UI_COLOR_THINKING, 0);
            lv_label_set_text(s_temp_status_label, "Warm");
        } else {
            lv_obj_set_style_text_color(s_temp_icon, UI_COLOR_SECONDARY, 0);
            lv_label_set_text(s_temp_status_label, "Normal");
        }
    }

    // Update GPS tile
    if (s_gps_status_label) {
        if (!info.gps_available) {
            lv_label_set_text(s_gps_status_label, "Not Available");
            lv_label_set_text(s_gps_coord_label, "");
            lv_obj_set_style_text_color(s_gps_icon, UI_COLOR_TEXT_DIM, 0);
        } else if (info.gps_fix) {
            lv_label_set_text(s_gps_status_label, "GPS Fixed");
            snprintf(buf, sizeof(buf), "%.6f, %.6f", info.gps_latitude, info.gps_longitude);
            lv_label_set_text(s_gps_coord_label, buf);
            lv_obj_set_style_text_color(s_gps_icon, UI_COLOR_SECONDARY, 0);
        } else {
            lv_label_set_text(s_gps_status_label, "Searching...");
            snprintf(buf, sizeof(buf), "Satellites: %d", info.gps_satellites);
            lv_label_set_text(s_gps_coord_label, buf);
            lv_obj_set_style_text_color(s_gps_icon, UI_COLOR_THINKING, 0);
        }
    }

    // Update DateTime tile
    if (s_time_label) {
        snprintf(buf, sizeof(buf), "%02d:%02d:%02d",
                 info.rtc_hour, info.rtc_minute, info.rtc_second);
        lv_label_set_text(s_time_label, buf);

        snprintf(buf, sizeof(buf), "%04d/%02d/%02d",
                 info.rtc_year, info.rtc_month, info.rtc_day);
        lv_label_set_text(s_date_label, buf);

        // Weekday names
        static const char *weekdays[] = {
            "Sunday", "Monday", "Tuesday", "Wednesday",
            "Thursday", "Friday", "Saturday"
        };
        if (info.rtc_weekday < 7) {
            lv_label_set_text(s_weekday_label, weekdays[info.rtc_weekday]);
        }
    }
}

void ui_info_carousel_start_auto_rotate(void)
{
    if (s_auto_rotate_timer) {
        lv_timer_resume(s_auto_rotate_timer);
        ESP_LOGI(TAG, "Auto-rotate started");
    }
}

void ui_info_carousel_stop_auto_rotate(void)
{
    if (s_auto_rotate_timer) {
        lv_timer_pause(s_auto_rotate_timer);
        ESP_LOGI(TAG, "Auto-rotate stopped");
    }
}

void ui_info_carousel_goto_page(carousel_page_t page, bool animate)
{
    if (!s_initialized || page >= CAROUSEL_PAGE_MAX) {
        return;
    }

    if (s_tiles[page]) {
        lv_tileview_set_tile(s_tileview, s_tiles[page],
                             animate ? LV_ANIM_ON : LV_ANIM_OFF);
        s_current_page = page;
        update_page_indicator(page);
    }
}

void ui_info_carousel_next_page(bool animate)
{
    carousel_page_t next = (s_current_page + 1) % CAROUSEL_PAGE_MAX;
    ui_info_carousel_goto_page(next, animate);
}

void ui_info_carousel_prev_page(bool animate)
{
    carousel_page_t prev = (s_current_page == 0) ?
                           (CAROUSEL_PAGE_MAX - 1) :
                           (s_current_page - 1);
    ui_info_carousel_goto_page(prev, animate);
}

carousel_page_t ui_info_carousel_get_page(void)
{
    return s_current_page;
}
