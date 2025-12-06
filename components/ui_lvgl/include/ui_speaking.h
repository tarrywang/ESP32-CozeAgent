/**
 * @file ui_speaking.h
 * @brief Speaking page UI component (AI response playback)
 */

#pragma once

#include "lvgl.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Create speaking page
 *
 * @param parent Parent object (screen)
 * @return Created page object
 */
lv_obj_t *ui_speaking_create(lv_obj_t *parent);

/**
 * @brief Destroy speaking page
 */
void ui_speaking_destroy(void);

/**
 * @brief Enter speaking page
 */
void ui_speaking_enter(void);

/**
 * @brief Exit speaking page
 */
void ui_speaking_exit(void);

/**
 * @brief Update response text display
 *
 * @param text Response text to display
 */
void ui_speaking_update_text(const char *text);

/**
 * @brief Append text to response display
 *
 * @param text Text to append
 */
void ui_speaking_append_text(const char *text);

/**
 * @brief Clear response text
 */
void ui_speaking_clear_text(void);

/**
 * @brief Update audio playback indicator
 *
 * @param level Audio level (0-100)
 */
void ui_speaking_update_level(uint8_t level);

/**
 * @brief Scroll text display to bottom
 */
void ui_speaking_scroll_to_bottom(void);

#ifdef __cplusplus
}
#endif
