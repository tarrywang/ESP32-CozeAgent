/**
 * @file ui_listening.c
 * @brief Listening page implementation
 */

#include "ui_listening.h"
#include "ui_manager.h"
#include "esp_log.h"

static const char *TAG = "UI_LISTENING";

// ============================================
// Private Variables
// ============================================

static lv_obj_t *s_page = NULL;
static lv_obj_t *s_title_label = NULL;
static lv_obj_t *s_wave_bars[5] = {NULL};  // Audio level visualization bars
static lv_obj_t *s_transcript_label = NULL;
static uint8_t s_current_level = 0;

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
 * @brief Update wave bar heights based on audio level
 */
static void update_wave_bars(uint8_t level)
{
    // Create wave-like pattern based on audio level
    static const int base_heights[] = {30, 50, 40, 55, 35};
    static int offsets[] = {0, 10, 5, 15, 3};

    for (int i = 0; i < 5; i++) {
        if (s_wave_bars[i]) {
            // Calculate height with some variation
            int height = base_heights[i] + (level * base_heights[i] / 100);
            offsets[i] = (offsets[i] + 5 + level / 20) % 20;
            height += offsets[i] - 10;

            if (height < 15) height = 15;
            if (height > 100) height = 100;

            lv_obj_set_height(s_wave_bars[i], height);
        }
    }
}

// ============================================
// Public Functions
// ============================================

lv_obj_t *ui_listening_create(lv_obj_t *parent)
{
    ESP_LOGI(TAG, "Creating listening page");

    // Main container
    s_page = lv_obj_create(parent);
    lv_obj_set_size(s_page, UI_SCREEN_WIDTH, UI_SCREEN_HEIGHT);
    lv_obj_set_style_bg_color(s_page, UI_COLOR_BG, 0);
    lv_obj_set_style_border_width(s_page, 0, 0);
    lv_obj_clear_flag(s_page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(s_page);

    // Title "Listening..."
    s_title_label = lv_label_create(s_page);
    lv_label_set_text(s_title_label, "Listening...");
    lv_obj_set_style_text_color(s_title_label, UI_COLOR_LISTENING, 0);
    lv_obj_set_style_text_font(s_title_label, &lv_font_montserrat_24, 0);
    lv_obj_align(s_title_label, LV_ALIGN_TOP_MID, 0, 80);

    // Wave bar container
    lv_obj_t *wave_container = lv_obj_create(s_page);
    lv_obj_set_size(wave_container, 200, 120);
    lv_obj_set_style_bg_opa(wave_container, LV_OPA_0, 0);
    lv_obj_set_style_border_width(wave_container, 0, 0);
    lv_obj_center(wave_container);
    lv_obj_clear_flag(wave_container, LV_OBJ_FLAG_SCROLLABLE);

    // Create 5 wave bars
    for (int i = 0; i < 5; i++) {
        s_wave_bars[i] = lv_obj_create(wave_container);
        lv_obj_set_size(s_wave_bars[i], 20, 40);
        lv_obj_set_style_bg_color(s_wave_bars[i], UI_COLOR_LISTENING, 0);
        lv_obj_set_style_radius(s_wave_bars[i], 10, 0);
        lv_obj_set_style_border_width(s_wave_bars[i], 0, 0);
        lv_obj_align(s_wave_bars[i], LV_ALIGN_CENTER, (i - 2) * 35, 0);
    }

    // Transcript label (shows recognized speech)
    s_transcript_label = lv_label_create(s_page);
    lv_label_set_text(s_transcript_label, "");
    lv_label_set_long_mode(s_transcript_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_transcript_label, UI_SCREEN_WIDTH - 80);
    lv_obj_set_style_text_color(s_transcript_label, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(s_transcript_label, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_align(s_transcript_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_transcript_label, LV_ALIGN_CENTER, 0, 100);

    // Hint at bottom
    lv_obj_t *hint = lv_label_create(s_page);
    lv_label_set_text(hint, "Tap to cancel");
    lv_obj_set_style_text_color(hint, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -40);

    return s_page;
}

void ui_listening_destroy(void)
{
    if (s_page) {
        s_page = NULL;
        s_title_label = NULL;
        s_transcript_label = NULL;
        for (int i = 0; i < 5; i++) {
            s_wave_bars[i] = NULL;
        }
    }
}

void ui_listening_enter(void)
{
    ESP_LOGI(TAG, "Entering listening page");

    // Reset state
    s_current_level = 0;
    if (s_transcript_label) {
        lv_label_set_text(s_transcript_label, "");
    }

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

void ui_listening_exit(void)
{
    ESP_LOGI(TAG, "Exiting listening page");

    // 🛡️ Third layer of defense: Early return if display not available
    extern lv_display_t *app_get_display(void);
    if (app_get_display() == NULL) {
        return;
    }

    // Currently no LVGL cleanup needed, but guard is in place for future changes
}

void ui_listening_update_level(uint8_t level)
{
    s_current_level = level;
    update_wave_bars(level);
}

void ui_listening_update_text(const char *text)
{
    if (s_transcript_label && text) {
        lv_label_set_text(s_transcript_label, text);
    }
}

void ui_listening_clear_text(void)
{
    if (s_transcript_label) {
        lv_label_set_text(s_transcript_label, "");
    }
}
