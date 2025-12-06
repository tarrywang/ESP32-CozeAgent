/**
 * @file app_core.h
 * @brief Application core state machine and task orchestration
 *
 * Manages the voice assistant state machine and coordinates
 * between audio, UI, and Coze WebSocket components.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================
// Application States
// ============================================

/**
 * @brief Application state machine states
 */
typedef enum {
    APP_STATE_INIT = 0,         // Initializing
    APP_STATE_IDLE,             // Ready, waiting for user input
    APP_STATE_LISTENING,        // Recording user speech
    APP_STATE_PROCESSING,       // Sending to Coze, waiting for response
    APP_STATE_SPEAKING,         // Playing AI response
    APP_STATE_ERROR,            // Error state
} app_state_t;

/**
 * @brief Application events
 */
typedef enum {
    APP_EVENT_NONE = 0,
    APP_EVENT_USER_TAP,             // User tapped screen
    APP_EVENT_USER_LONG_PRESS,      // User long pressed
    APP_EVENT_BUTTON_PRESS,         // Hardware button pressed
    APP_EVENT_VOICE_START,          // VAD detected voice start
    APP_EVENT_VOICE_END,            // VAD detected voice end
    APP_EVENT_COZE_RESPONSE_START,  // Coze started sending response
    APP_EVENT_COZE_RESPONSE_END,    // Coze finished response
    APP_EVENT_COZE_ERROR,           // Coze error
    APP_EVENT_AUDIO_DONE,           // Audio playback finished
    APP_EVENT_WIFI_CONNECTED,       // WiFi connected
    APP_EVENT_WIFI_DISCONNECTED,    // WiFi disconnected
    APP_EVENT_CANCEL,               // User cancelled current operation
} app_event_t;

/**
 * @brief State change callback function type
 */
typedef void (*app_state_callback_t)(app_state_t old_state, app_state_t new_state, void *user_data);

// ============================================
// Application Core Function Declarations
// ============================================

/**
 * @brief Initialize application core
 *
 * @return ESP_OK on success
 */
esp_err_t app_core_init(void);

/**
 * @brief Deinitialize application core
 *
 * @return ESP_OK on success
 */
esp_err_t app_core_deinit(void);

/**
 * @brief Start application core task
 *
 * @return ESP_OK on success
 */
esp_err_t app_core_start_task(void);

/**
 * @brief Stop application core task
 *
 * @return ESP_OK on success
 */
esp_err_t app_core_stop_task(void);

/**
 * @brief Get current application state
 *
 * @return Current state
 */
app_state_t app_core_get_state(void);

/**
 * @brief Set application state (force state change)
 *
 * @param state New state
 * @return ESP_OK on success
 */
esp_err_t app_core_set_state(app_state_t state);

/**
 * @brief Send event to state machine
 *
 * @param event Event to process
 * @return ESP_OK on success
 */
esp_err_t app_core_send_event(app_event_t event);

/**
 * @brief Register state change callback
 *
 * @param callback Callback function
 * @param user_data User context
 * @return ESP_OK on success
 */
esp_err_t app_core_register_callback(app_state_callback_t callback, void *user_data);

/**
 * @brief Get state name string
 *
 * @param state State
 * @return State name string
 */
const char *app_core_state_to_string(app_state_t state);

/**
 * @brief Get event name string
 *
 * @param event Event
 * @return Event name string
 */
const char *app_core_event_to_string(app_event_t event);

/**
 * @brief Start listening for user input
 *
 * Transitions from IDLE to LISTENING state.
 *
 * @return ESP_OK on success
 */
esp_err_t app_core_start_listening(void);

/**
 * @brief Stop listening and process
 *
 * Transitions from LISTENING to PROCESSING state.
 *
 * @return ESP_OK on success
 */
esp_err_t app_core_stop_listening(void);

/**
 * @brief Cancel current operation
 *
 * Returns to IDLE state.
 *
 * @return ESP_OK on success
 */
esp_err_t app_core_cancel(void);

/**
 * @brief Check if system is ready for voice input
 *
 * @return true if ready
 */
bool app_core_is_ready(void);

#ifdef __cplusplus
}
#endif
