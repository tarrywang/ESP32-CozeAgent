/**
 * @file ui_thinking.h
 * @brief Thinking/processing page UI component
 */

#pragma once

#include "lvgl.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Create thinking page
 *
 * @param parent Parent object (screen)
 * @return Created page object
 */
lv_obj_t *ui_thinking_create(lv_obj_t *parent);

/**
 * @brief Destroy thinking page
 */
void ui_thinking_destroy(void);

/**
 * @brief Enter thinking page
 */
void ui_thinking_enter(void);

/**
 * @brief Exit thinking page
 */
void ui_thinking_exit(void);

/**
 * @brief Update thinking animation
 *
 * Called periodically to animate the thinking indicator.
 */
void ui_thinking_update(void);

/**
 * @brief Set thinking status text
 *
 * @param text Status text (e.g., "Processing...", "Generating response...")
 */
void ui_thinking_set_text(const char *text);

#ifdef __cplusplus
}
#endif
