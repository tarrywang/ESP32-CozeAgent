/**
 * @file ui_speaking.c
 * @brief Speaking page implementation (AI response display)
 */

#include "ui_speaking.h"
#include "ui_manager.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "UI_SPEAKING";

// ============================================
// Configuration
// ============================================

#define MAX_TRANSCRIPT_LEN  2048

// ============================================
// Private Variables
// ============================================

static lv_obj_t *s_page = NULL;
static lv_obj_t *s_title_label = NULL;
static lv_obj_t *s_transcript_container = NULL;
static lv_obj_t *s_transcript_label = NULL;
static lv_obj_t *s_wave_bars[5] = {NULL};
static char s_transcript_buffer[MAX_TRANSCRIPT_LEN] = {0};
static size_t s_transcript_len = 0;

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
 * @brief Update wave bar heights
 */
static void update_wave_bars(uint8_t level)
{
    static const int base_heights[] = {20, 35, 28, 40, 25};
    static int offsets[] = {0, 8, 3, 12, 5};

    for (int i = 0; i < 5; i++) {
        if (s_wave_bars[i]) {
            int height = base_heights[i] + (level * base_heights[i] / 150);
            offsets[i] = (offsets[i] + 3 + level / 30) % 15;
            height += offsets[i] - 7;

            if (height < 10) height = 10;
            if (height > 60) height = 60;

            lv_obj_set_height(s_wave_bars[i], height);
        }
    }
}

// ============================================
// Public Functions
// ============================================

lv_obj_t *ui_speaking_create(lv_obj_t *parent)
{
    ESP_LOGI(TAG, "Creating speaking page");

    // Main container
    s_page = lv_obj_create(parent);
    lv_obj_set_size(s_page, UI_SCREEN_WIDTH, UI_SCREEN_HEIGHT);
    lv_obj_set_style_bg_color(s_page, UI_COLOR_BG, 0);
    lv_obj_set_style_border_width(s_page, 0, 0);
    lv_obj_clear_flag(s_page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(s_page);

    // Title "AI Response"
    s_title_label = lv_label_create(s_page);
    lv_label_set_text(s_title_label, "AI Response");
    lv_obj_set_style_text_color(s_title_label, UI_COLOR_SPEAKING, 0);
    lv_obj_set_style_text_font(s_title_label, &lv_font_montserrat_20, 0);
    lv_obj_align(s_title_label, LV_ALIGN_TOP_MID, 0, 60);

    // Wave bars container (small, at top)
    lv_obj_t *wave_container = lv_obj_create(s_page);
    lv_obj_set_size(wave_container, 120, 60);
    lv_obj_set_style_bg_opa(wave_container, LV_OPA_0, 0);
    lv_obj_set_style_border_width(wave_container, 0, 0);
    lv_obj_align(wave_container, LV_ALIGN_TOP_MID, 0, 100);
    lv_obj_clear_flag(wave_container, LV_OBJ_FLAG_SCROLLABLE);

    // Create 5 wave bars
    for (int i = 0; i < 5; i++) {
        s_wave_bars[i] = lv_obj_create(wave_container);
        lv_obj_set_size(s_wave_bars[i], 12, 25);
        lv_obj_set_style_bg_color(s_wave_bars[i], UI_COLOR_SPEAKING, 0);
        lv_obj_set_style_radius(s_wave_bars[i], 6, 0);
        lv_obj_set_style_border_width(s_wave_bars[i], 0, 0);
        lv_obj_align(s_wave_bars[i], LV_ALIGN_CENTER, (i - 2) * 22, 0);
    }

    // Transcript container (scrollable)
    s_transcript_container = lv_obj_create(s_page);
    lv_obj_set_size(s_transcript_container, UI_SCREEN_WIDTH - 60, 200);
    lv_obj_set_style_bg_color(s_transcript_container, lv_color_hex(0x111111), 0);
    lv_obj_set_style_bg_opa(s_transcript_container, LV_OPA_80, 0);
    lv_obj_set_style_border_width(s_transcript_container, 0, 0);
    lv_obj_set_style_radius(s_transcript_container, 15, 0);
    lv_obj_set_style_pad_all(s_transcript_container, 15, 0);
    lv_obj_align(s_transcript_container, LV_ALIGN_CENTER, 0, 30);
    lv_obj_set_scroll_dir(s_transcript_container, LV_DIR_VER);

    // Transcript label inside container
    s_transcript_label = lv_label_create(s_transcript_container);
    lv_label_set_text(s_transcript_label, "");
    lv_label_set_long_mode(s_transcript_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_transcript_label, lv_obj_get_content_width(s_transcript_container));
    lv_obj_set_style_text_color(s_transcript_label, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(s_transcript_label, &lv_font_montserrat_16, 0);
    lv_obj_align(s_transcript_label, LV_ALIGN_TOP_LEFT, 0, 0);

    // Hint at bottom
    lv_obj_t *hint = lv_label_create(s_page);
    lv_label_set_text(hint, "Tap to interrupt");
    lv_obj_set_style_text_color(hint, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -30);

    return s_page;
}

void ui_speaking_destroy(void)
{
    if (s_page) {
        s_page = NULL;
        s_title_label = NULL;
        s_transcript_container = NULL;
        s_transcript_label = NULL;
        for (int i = 0; i < 5; i++) {
            s_wave_bars[i] = NULL;
        }
    }
    s_transcript_len = 0;
    s_transcript_buffer[0] = '\0';
}

void ui_speaking_enter(void)
{
    ESP_LOGI(TAG, "Entering speaking page");

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

void ui_speaking_exit(void)
{
    ESP_LOGI(TAG, "Exiting speaking page");

    // 🛡️ Third layer of defense: Early return if display not available
    extern lv_display_t *app_get_display(void);
    if (app_get_display() == NULL) {
        return;
    }

    // Currently no LVGL cleanup needed, but guard is in place for future changes
}

void ui_speaking_update_text(const char *text)
{
    if (s_transcript_label && text) {
        // Replace entire text
        strncpy(s_transcript_buffer, text, MAX_TRANSCRIPT_LEN - 1);
        s_transcript_buffer[MAX_TRANSCRIPT_LEN - 1] = '\0';
        s_transcript_len = strlen(s_transcript_buffer);
        lv_label_set_text(s_transcript_label, s_transcript_buffer);
        ui_speaking_scroll_to_bottom();
    }
}

void ui_speaking_append_text(const char *text)
{
    if (s_transcript_label && text) {
        size_t append_len = strlen(text);
        if (s_transcript_len + append_len < MAX_TRANSCRIPT_LEN - 1) {
            strcat(s_transcript_buffer, text);
            s_transcript_len += append_len;
            lv_label_set_text(s_transcript_label, s_transcript_buffer);
            ui_speaking_scroll_to_bottom();
        }
    }
}

void ui_speaking_clear_text(void)
{
    s_transcript_buffer[0] = '\0';
    s_transcript_len = 0;
    if (s_transcript_label) {
        lv_label_set_text(s_transcript_label, "");
    }
}

void ui_speaking_update_level(uint8_t level)
{
    update_wave_bars(level);
}

void ui_speaking_scroll_to_bottom(void)
{
    if (s_transcript_container) {
        lv_obj_scroll_to_y(s_transcript_container, LV_COORD_MAX, LV_ANIM_ON);
    }
}
