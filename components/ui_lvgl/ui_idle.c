/**
 * @file ui_idle.c
 * @brief Idle page implementation
 */

#include "ui_idle.h"
#include "ui_manager.h"
#include "esp_log.h"
#include <time.h>

static const char *TAG = "UI_IDLE";

// ============================================
// Private Variables
// ============================================

static lv_obj_t *s_page = NULL;
static lv_obj_t *s_main_icon = NULL;
static lv_obj_t *s_hint_label = NULL;
static lv_obj_t *s_pulse_circle = NULL;
static lv_anim_t s_pulse_anim;

// ============================================
// Private Functions
// ============================================

/**
 * @brief Page opacity animation callback (LVGL 9.x compatible)
 */
static void page_opa_anim_cb(void *var, int32_t value)
{
    lv_obj_t *obj = (lv_obj_t *)var;
    if (obj) {
        lv_obj_set_style_opa(obj, (lv_opa_t)value, 0);
    }
}

/**
 * @brief Pulse animation callback
 * Note: Only changes opacity to minimize redraw area and reduce flickering
 */
static void pulse_anim_cb(void *var, int32_t value)
{
    if (s_pulse_circle) {
        // Only change opacity - avoid size changes to reduce flickering
        lv_obj_set_style_opa(s_pulse_circle, value, 0);
    }
}

/**
 * @brief Start pulse animation
 */
static void start_pulse_animation(void)
{
    lv_anim_init(&s_pulse_anim);
    lv_anim_set_var(&s_pulse_anim, s_pulse_circle);
    lv_anim_set_values(&s_pulse_anim, 255, 0);
    lv_anim_set_time(&s_pulse_anim, 2000);
    lv_anim_set_repeat_count(&s_pulse_anim, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_exec_cb(&s_pulse_anim, pulse_anim_cb);
    lv_anim_start(&s_pulse_anim);
}

// ============================================
// Public Functions
// ============================================

lv_obj_t *ui_idle_create(lv_obj_t *parent)
{
    ESP_LOGI(TAG, "Creating idle page");

    // Main container
    s_page = lv_obj_create(parent);
    lv_obj_set_size(s_page, UI_SCREEN_WIDTH, UI_SCREEN_HEIGHT);
    lv_obj_set_style_bg_color(s_page, UI_COLOR_BG, 0);
    lv_obj_set_style_border_width(s_page, 0, 0);
    lv_obj_clear_flag(s_page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(s_page);

    // Pulse circle (behind main icon)
    s_pulse_circle = lv_obj_create(s_page);
    lv_obj_set_size(s_pulse_circle, 100, 100);
    lv_obj_set_style_bg_color(s_pulse_circle, UI_COLOR_PRIMARY, 0);
    lv_obj_set_style_bg_opa(s_pulse_circle, LV_OPA_50, 0);
    lv_obj_set_style_radius(s_pulse_circle, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(s_pulse_circle, 0, 0);
    lv_obj_center(s_pulse_circle);

    // Main microphone icon
    s_main_icon = lv_label_create(s_page);
    lv_label_set_text(s_main_icon, LV_SYMBOL_AUDIO);
    lv_obj_set_style_text_color(s_main_icon, UI_COLOR_PRIMARY, 0);
    lv_obj_set_style_text_font(s_main_icon, &lv_font_montserrat_32, 0);
    lv_obj_center(s_main_icon);

    // "Tap to speak" hint
    s_hint_label = lv_label_create(s_page);
    lv_label_set_text(s_hint_label, "Tap to speak");
    lv_obj_set_style_text_color(s_hint_label, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(s_hint_label, &lv_font_montserrat_16, 0);
    lv_obj_align(s_hint_label, LV_ALIGN_CENTER, 0, 80);

    // "Coze AI" branding at bottom
    lv_obj_t *brand = lv_label_create(s_page);
    lv_label_set_text(brand, "Powered by Coze AI");
    lv_obj_set_style_text_color(brand, lv_color_hex(0x444444), 0);
    lv_obj_set_style_text_font(brand, &lv_font_montserrat_14, 0);
    lv_obj_align(brand, LV_ALIGN_BOTTOM_MID, 0, -30);

    return s_page;
}

void ui_idle_destroy(void)
{
    if (s_page) {
        lv_anim_del(s_pulse_circle, NULL);
        // Page will be deleted by parent
        s_page = NULL;
        s_main_icon = NULL;
        s_hint_label = NULL;
        s_pulse_circle = NULL;
    }
}

void ui_idle_enter(void)
{
    ESP_LOGI(TAG, "Entering idle page");

    // Start pulse animation
    if (s_pulse_circle) {
        start_pulse_animation();
    }

    // Fade in animation
    if (s_page) {
        lv_obj_set_style_opa(s_page, LV_OPA_0, 0);
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, s_page);
        lv_anim_set_values(&a, LV_OPA_0, LV_OPA_COVER);
        lv_anim_set_time(&a, UI_ANIM_DURATION_NORMAL);
        lv_anim_set_exec_cb(&a, page_opa_anim_cb);
        lv_anim_start(&a);
    }
}

void ui_idle_exit(void)
{
    ESP_LOGI(TAG, "Exiting idle page");

    // 🛡️ Third layer of defense: Validate object and display
    extern lv_display_t *app_get_display(void);
    if (s_pulse_circle && app_get_display() != NULL) {
        lv_anim_del(s_pulse_circle, NULL);
    }
}

void ui_idle_update_time(void)
{
    // Update time display (if implemented in status bar)
    // This would be called periodically
}

void ui_idle_update_status(bool wifi_connected, uint8_t battery_level)
{
    // Status updates handled by ui_manager
}
