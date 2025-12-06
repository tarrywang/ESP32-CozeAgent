/**
 * @file bsp_button.h
 * @brief BOOT Button Support for Waveshare ESP32-S3 AMOLED Board
 */

#pragma once

#include "esp_err.h"
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Button event types
 */
typedef enum {
    BSP_BUTTON_EVENT_PRESSED,      /**< Button pressed */
    BSP_BUTTON_EVENT_RELEASED,     /**< Button released */
    BSP_BUTTON_EVENT_SHORT_CLICK,  /**< Short click detected */
    BSP_BUTTON_EVENT_LONG_PRESS,   /**< Long press detected (>1s) */
} bsp_button_event_t;

/**
 * @brief Button callback function type
 *
 * @param event Button event type
 * @param user_data User data passed to callback
 */
typedef void (*bsp_button_callback_t)(bsp_button_event_t event, void *user_data);

/**
 * @brief Initialize BOOT button
 *
 * @param callback Callback function for button events (can be NULL)
 * @param user_data User data to pass to callback
 * @return esp_err_t ESP_OK on success
 */
esp_err_t bsp_button_init(bsp_button_callback_t callback, void *user_data);

/**
 * @brief Deinitialize button
 *
 * @return esp_err_t ESP_OK on success
 */
esp_err_t bsp_button_deinit(void);

/**
 * @brief Get current button state
 *
 * @return true if button is pressed, false otherwise
 */
bool bsp_button_is_pressed(void);

#ifdef __cplusplus
}
#endif
