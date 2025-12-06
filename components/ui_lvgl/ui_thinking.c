/**
 * @file ui_thinking.c
 * @brief Thinking/processing page implementation (LVGL 9.x)
 */

#include "ui_thinking.h"
#include "ui_manager.h"
#include "esp_log.h"

static const char *TAG = "UI_THINKING";

// ============================================
// Private Variables
// ============================================

static lv_obj_t *s_page = NULL;
static lv_obj_t *s_title_label = NULL;
static lv_obj_t *s_spinner = NULL;
static lv_obj_t *s_status_label = NULL;
static lv_obj_t *s_dots[3] = {NULL};  // Animated dots
static lv_anim_t s_dot_anims[3];

// ============================================
// Private Functions
// ============================================

/**
 * @brief Dot animation callback
 */
static void dot_anim_cb(void *var, int32_t value)
{
    lv_obj_t *dot = (lv_obj_t *)var;
    if (dot) {
        lv_obj_set_style_opa(dot, (lv_opa_t)value, 0);
    }
}

/**
 * @brief Page opacity animation callback
 */
static void page_opa_anim_cb(void *var, int32_t value)
{
    lv_obj_t *obj = (lv_obj_t *)var;
    if (obj) {
        lv_obj_set_style_opa(obj, (lv_opa_t)value, 0);
    }
}

/**
 * @brief Start dots animation
 */
static void start_dots_animation(void)
{
    for (int i = 0; i < 3; i++) {
        if (s_dots[i]) {
            lv_anim_init(&s_dot_anims[i]);
            lv_anim_set_var(&s_dot_anims[i], s_dots[i]);
            lv_anim_set_values(&s_dot_anims[i], 50, 255);
            lv_anim_set_time(&s_dot_anims[i], 600);
            lv_anim_set_delay(&s_dot_anims[i], i * 200);  // Staggered delay
            lv_anim_set_repeat_count(&s_dot_anims[i], LV_ANIM_REPEAT_INFINITE);
            lv_anim_set_playback_time(&s_dot_anims[i], 600);
            lv_anim_set_exec_cb(&s_dot_anims[i], dot_anim_cb);
            lv_anim_start(&s_dot_anims[i]);
        }
    }
}

/**
 * @brief Stop dots animation
 */
static void stop_dots_animation(void)
{
    for (int i = 0; i < 3; i++) {
        if (s_dots[i]) {
            lv_anim_del(s_dots[i], NULL);
        }
    }
}

// ============================================
// Public Functions
// ============================================

lv_obj_t *ui_thinking_create(lv_obj_t *parent)
{
    ESP_LOGI(TAG, "Creating thinking page");

    // Main container
    s_page = lv_obj_create(parent);
    lv_obj_set_size(s_page, UI_SCREEN_WIDTH, UI_SCREEN_HEIGHT);
    lv_obj_set_style_bg_color(s_page, UI_COLOR_BG, 0);
    lv_obj_set_style_border_width(s_page, 0, 0);
    lv_obj_clear_flag(s_page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(s_page);

    // Title "Thinking"
    s_title_label = lv_label_create(s_page);
    lv_label_set_text(s_title_label, "Thinking");
    lv_obj_set_style_text_color(s_title_label, UI_COLOR_THINKING, 0);
    lv_obj_set_style_text_font(s_title_label, &lv_font_montserrat_24, 0);
    lv_obj_align(s_title_label, LV_ALIGN_TOP_MID, 0, 80);

    // Animated dots container
    lv_obj_t *dots_container = lv_obj_create(s_page);
    lv_obj_set_size(dots_container, 80, 20);
    lv_obj_set_style_bg_opa(dots_container, LV_OPA_0, 0);
    lv_obj_set_style_border_width(dots_container, 0, 0);
    lv_obj_align(dots_container, LV_ALIGN_TOP_MID, 60, 85);
    lv_obj_clear_flag(dots_container, LV_OBJ_FLAG_SCROLLABLE);

    // Create 3 dots
    for (int i = 0; i < 3; i++) {
        s_dots[i] = lv_obj_create(dots_container);
        lv_obj_set_size(s_dots[i], 8, 8);
        lv_obj_set_style_bg_color(s_dots[i], UI_COLOR_THINKING, 0);
        lv_obj_set_style_radius(s_dots[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(s_dots[i], 0, 0);
        lv_obj_align(s_dots[i], LV_ALIGN_LEFT_MID, i * 15, 0);
    }

    // Spinner (LVGL 9.x API)
    s_spinner = lv_spinner_create(s_page);
    lv_obj_set_size(s_spinner, 80, 80);
    lv_obj_center(s_spinner);
    lv_spinner_set_anim_params(s_spinner, 1000, 60);  // 1000ms period, 60 degree arc
    lv_obj_set_style_arc_color(s_spinner, UI_COLOR_THINKING, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_spinner, UI_COLOR_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_spinner, 6, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(s_spinner, 6, LV_PART_MAIN);

    // Status label
    s_status_label = lv_label_create(s_page);
    lv_label_set_text(s_status_label, "Processing your request...");
    lv_obj_set_style_text_color(s_status_label, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(s_status_label, &lv_font_montserrat_16, 0);
    lv_obj_align(s_status_label, LV_ALIGN_CENTER, 0, 80);

    // Hint at bottom
    lv_obj_t *hint = lv_label_create(s_page);
    lv_label_set_text(hint, "Tap to cancel");
    lv_obj_set_style_text_color(hint, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -40);

    return s_page;
}

void ui_thinking_destroy(void)
{
    if (s_page) {
        stop_dots_animation();
        s_page = NULL;
        s_title_label = NULL;
        s_spinner = NULL;
        s_status_label = NULL;
        for (int i = 0; i < 3; i++) {
            s_dots[i] = NULL;
        }
    }
}

void ui_thinking_enter(void)
{
    ESP_LOGI(TAG, "Entering thinking page");

    // Start dots animation
    start_dots_animation();

    // Fade in animation
    if (s_page) {
        lv_obj_set_style_opa(s_page, LV_OPA_0, 0);
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, s_page);
        lv_anim_set_values(&a, LV_OPA_0, LV_OPA_COVER);
        lv_anim_set_time(&a, UI_ANIM_DURATION_FAST);
        lv_anim_set_exec_cb(&a, page_opa_anim_cb);
        lv_anim_start(&a);
    }
}

void ui_thinking_exit(void)
{
    ESP_LOGI(TAG, "Exiting thinking page");

    // 🛡️ Third layer of defense: Check display before stopping animations
    extern lv_display_t *app_get_display(void);
    if (app_get_display() != NULL) {
        stop_dots_animation();
    }
}

void ui_thinking_update(void)
{
    // Animation handled by LVGL timer automatically
}

void ui_thinking_set_text(const char *text)
{
    if (s_status_label && text) {
        lv_label_set_text(s_status_label, text);
    }
}
