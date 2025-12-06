/**
 * @file ui_listening.h
 * @brief Listening page UI component
 */

#pragma once

#include "lvgl.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Create listening page
 *
 * @param parent Parent object (screen)
 * @return Created page object
 */
lv_obj_t *ui_listening_create(lv_obj_t *parent);

/**
 * @brief Destroy listening page
 */
void ui_listening_destroy(void);

/**
 * @brief Enter listening page
 */
void ui_listening_enter(void);

/**
 * @brief Exit listening page
 */
void ui_listening_exit(void);

/**
 * @brief Update audio level visualization
 *
 * @param level Audio level (0-100)
 */
void ui_listening_update_level(uint8_t level);

/**
 * @brief Update transcript text
 *
 * @param text Recognized text
 */
void ui_listening_update_text(const char *text);

/**
 * @brief Clear transcript
 */
void ui_listening_clear_text(void);

#ifdef __cplusplus
}
#endif
