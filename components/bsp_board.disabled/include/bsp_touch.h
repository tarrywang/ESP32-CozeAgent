/**
 * @file bsp_touch.h
 * @brief Touch controller driver interface for CST9217
 *
 * Provides touch input handling, gesture detection, and LVGL integration
 * for the capacitive touch panel.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================
// Touch Configuration
// ============================================

#define BSP_TOUCH_MAX_POINTS    5   // Maximum touch points supported

/**
 * @brief Touch point data structure
 */
typedef struct {
    uint16_t x;             // X coordinate
    uint16_t y;             // Y coordinate
    uint8_t id;             // Touch point ID
    uint8_t pressure;       // Touch pressure (0-255)
    bool valid;             // Whether this point is valid
} bsp_touch_point_t;

/**
 * @brief Touch data structure (multi-touch)
 */
typedef struct {
    uint8_t point_count;                        // Number of active touch points
    bsp_touch_point_t points[BSP_TOUCH_MAX_POINTS];  // Touch point data
} bsp_touch_data_t;

/**
 * @brief Touch gesture types
 */
typedef enum {
    BSP_TOUCH_GESTURE_NONE = 0,
    BSP_TOUCH_GESTURE_TAP,
    BSP_TOUCH_GESTURE_DOUBLE_TAP,
    BSP_TOUCH_GESTURE_LONG_PRESS,
    BSP_TOUCH_GESTURE_SWIPE_UP,
    BSP_TOUCH_GESTURE_SWIPE_DOWN,
    BSP_TOUCH_GESTURE_SWIPE_LEFT,
    BSP_TOUCH_GESTURE_SWIPE_RIGHT,
    BSP_TOUCH_GESTURE_PINCH_IN,
    BSP_TOUCH_GESTURE_PINCH_OUT,
} bsp_touch_gesture_t;

/**
 * @brief Touch event callback function type
 *
 * @param data Touch data
 * @param gesture Detected gesture
 */
typedef void (*bsp_touch_callback_t)(const bsp_touch_data_t *data,
                                      bsp_touch_gesture_t gesture);

/**
 * @brief Touch configuration structure
 */
typedef struct {
    uint16_t width;                 // Touch panel width (should match display)
    uint16_t height;                // Touch panel height
    bool swap_xy;                   // Swap X and Y coordinates
    bool invert_x;                  // Invert X axis
    bool invert_y;                  // Invert Y axis
    bsp_touch_callback_t callback;  // Optional callback for touch events
} bsp_touch_config_t;

// Default touch configuration
#define BSP_TOUCH_DEFAULT_CONFIG() { \
    .width = 466,                    \
    .height = 466,                   \
    .swap_xy = false,                \
    .invert_x = false,               \
    .invert_y = false,               \
    .callback = NULL,                \
}

// ============================================
// Touch Function Declarations
// ============================================

/**
 * @brief Initialize touch controller
 *
 * Initializes I2C communication with CST9217 and configures touch parameters.
 *
 * @param config Touch configuration (NULL for defaults)
 * @return ESP_OK on success
 */
esp_err_t bsp_touch_init(const bsp_touch_config_t *config);

/**
 * @brief Deinitialize touch controller
 *
 * @return ESP_OK on success
 */
esp_err_t bsp_touch_deinit(void);

/**
 * @brief Read current touch data
 *
 * Reads touch points from the controller. Non-blocking.
 *
 * @param data Pointer to store touch data
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if no touch
 */
esp_err_t bsp_touch_read(bsp_touch_data_t *data);

/**
 * @brief Check if screen is being touched
 *
 * @return true if touch is active
 */
bool bsp_touch_is_pressed(void);

/**
 * @brief Register touch event callback
 *
 * @param callback Callback function, or NULL to unregister
 * @return ESP_OK on success
 */
esp_err_t bsp_touch_register_callback(bsp_touch_callback_t callback);

/**
 * @brief Get last detected gesture
 *
 * @return Last gesture type
 */
bsp_touch_gesture_t bsp_touch_get_gesture(void);

/**
 * @brief Set touch sensitivity
 *
 * @param sensitivity Sensitivity level (0-100)
 * @return ESP_OK on success
 */
esp_err_t bsp_touch_set_sensitivity(uint8_t sensitivity);

/**
 * @brief Enable/disable touch controller
 *
 * @param enable true to enable, false to disable
 * @return ESP_OK on success
 */
esp_err_t bsp_touch_enable(bool enable);

/**
 * @brief Reset touch controller
 *
 * Performs software reset of the touch controller.
 *
 * @return ESP_OK on success
 */
esp_err_t bsp_touch_reset(void);

/**
 * @brief Get LVGL input device object
 *
 * @return Pointer to LVGL indev, or NULL if not initialized
 */
lv_indev_t *bsp_touch_get_lv_indev(void);

/**
 * @brief LVGL touch read callback
 *
 * This function is called by LVGL to read touch input.
 * Typically registered automatically during init.
 *
 * @param indev Input device
 * @param data Input data structure
 */
void bsp_touch_lv_read_cb(lv_indev_t *indev, lv_indev_data_t *data);

/**
 * @brief Calibrate touch panel
 *
 * Runs interactive calibration routine.
 *
 * @return ESP_OK on success
 */
esp_err_t bsp_touch_calibrate(void);

/**
 * @brief Get touch controller firmware version
 *
 * @return Firmware version, or 0 if not available
 */
uint16_t bsp_touch_get_firmware_version(void);

#ifdef __cplusplus
}
#endif
