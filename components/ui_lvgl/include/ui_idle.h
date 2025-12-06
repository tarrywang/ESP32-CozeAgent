/**
 * @file ui_idle.h
 * @brief Idle page UI component
 */

#pragma once

#include "lvgl.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Create idle page
 *
 * @param parent Parent object (screen)
 * @return Created page object
 */
lv_obj_t *ui_idle_create(lv_obj_t *parent);

/**
 * @brief Destroy idle page
 */
void ui_idle_destroy(void);

/**
 * @brief Enter idle page (show animation)
 */
void ui_idle_enter(void);

/**
 * @brief Exit idle page (hide animation)
 */
void ui_idle_exit(void);

/**
 * @brief Update idle page time display
 */
void ui_idle_update_time(void);

/**
 * @brief Update status indicators
 *
 * @param wifi_connected WiFi status
 * @param battery_level Battery percentage
 */
void ui_idle_update_status(bool wifi_connected, uint8_t battery_level);

#ifdef __cplusplus
}
#endif
