/**
 * @file bsp_button.h
 * @brief Button driver interface for hardware buttons
 *
 * Provides button event handling with debouncing, long press detection,
 * and callback mechanism.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================
// Button Configuration
// ============================================

/**
 * @brief Button identifiers
 */
typedef enum {
    BSP_BUTTON_BOOT_ID = 0,     // BOOT button (GPIO0)
    BSP_BUTTON_PWR_ID,          // Power button (if available)
    BSP_BUTTON_MAX
} bsp_button_id_t;

/**
 * @brief Button event types
 */
typedef enum {
    BSP_BUTTON_EVENT_NONE = 0,
    BSP_BUTTON_EVENT_PRESSED,       // Button pressed
    BSP_BUTTON_EVENT_RELEASED,      // Button released
    BSP_BUTTON_EVENT_CLICK,         // Single click
    BSP_BUTTON_EVENT_DOUBLE_CLICK,  // Double click
    BSP_BUTTON_EVENT_LONG_PRESS,    // Long press (>1 second)
    BSP_BUTTON_EVENT_HOLD,          // Button held down
} bsp_button_event_t;

/**
 * @brief Button state
 */
typedef struct {
    bool is_pressed;            // Current pressed state
    uint32_t press_time;        // Time when button was pressed (ms)
    uint32_t release_time;      // Time when button was released (ms)
    uint8_t click_count;        // Number of clicks in sequence
} bsp_button_state_t;

/**
 * @brief Button event callback function type
 *
 * @param button_id Button identifier
 * @param event Button event type
 * @param state Button state data
 */
typedef void (*bsp_button_callback_t)(bsp_button_id_t button_id,
                                       bsp_button_event_t event,
                                       const bsp_button_state_t *state);

/**
 * @brief Button configuration structure
 */
typedef struct {
    uint32_t debounce_ms;           // Debounce time in milliseconds
    uint32_t long_press_ms;         // Long press threshold in milliseconds
    uint32_t double_click_ms;       // Double click window in milliseconds
    bsp_button_callback_t callback; // Event callback function
} bsp_button_config_t;

// Default button configuration
#define BSP_BUTTON_DEFAULT_CONFIG() { \
    .debounce_ms = 20,                \
    .long_press_ms = 1000,            \
    .double_click_ms = 300,           \
    .callback = NULL,                 \
}

// ============================================
// Button Function Declarations
// ============================================

/**
 * @brief Initialize button driver
 *
 * Configures GPIO interrupts and button handling.
 *
 * @param config Button configuration (NULL for defaults)
 * @return ESP_OK on success
 */
esp_err_t bsp_button_init(const bsp_button_config_t *config);

/**
 * @brief Deinitialize button driver
 *
 * @return ESP_OK on success
 */
esp_err_t bsp_button_deinit(void);

/**
 * @brief Check if button is currently pressed
 *
 * @param button_id Button identifier
 * @return true if button is pressed
 */
bool bsp_button_is_pressed(bsp_button_id_t button_id);

/**
 * @brief Get button state
 *
 * @param button_id Button identifier
 * @param state Pointer to store button state
 * @return ESP_OK on success
 */
esp_err_t bsp_button_get_state(bsp_button_id_t button_id, bsp_button_state_t *state);

/**
 * @brief Register button event callback
 *
 * @param callback Callback function, or NULL to unregister
 * @return ESP_OK on success
 */
esp_err_t bsp_button_register_callback(bsp_button_callback_t callback);

/**
 * @brief Wait for button press
 *
 * Blocking function that waits for a button press.
 *
 * @param button_id Button identifier
 * @param timeout_ms Timeout in milliseconds (0 for indefinite)
 * @return ESP_OK on button press, ESP_ERR_TIMEOUT on timeout
 */
esp_err_t bsp_button_wait_press(bsp_button_id_t button_id, uint32_t timeout_ms);

/**
 * @brief Clear button event queue
 *
 * @return ESP_OK on success
 */
esp_err_t bsp_button_clear_events(void);

/**
 * @brief Get button name string
 *
 * @param button_id Button identifier
 * @return Button name string
 */
const char *bsp_button_get_name(bsp_button_id_t button_id);

/**
 * @brief Get event name string
 *
 * @param event Button event
 * @return Event name string
 */
const char *bsp_button_event_to_string(bsp_button_event_t event);

#ifdef __cplusplus
}
#endif
