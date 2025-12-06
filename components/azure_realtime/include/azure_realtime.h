/**
 * @file azure_realtime.h
 * @brief Azure OpenAI Realtime API WebSocket Client Interface
 *
 * Implements real-time voice conversation using Azure OpenAI Realtime API
 * Compatible interface with coze_ws for easy migration
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================
// Azure Realtime Client State
// ============================================

/**
 * @brief Azure Realtime WebSocket connection state
 */
typedef enum {
    AZURE_STATE_DISCONNECTED = 0,
    AZURE_STATE_CONNECTING,
    AZURE_STATE_CONNECTED,
    AZURE_STATE_READY,
    AZURE_STATE_STREAMING,
    AZURE_STATE_ERROR,
} azure_state_t;

/**
 * @brief Azure message types (for event callbacks)
 */
typedef enum {
    AZURE_MSG_TYPE_UNKNOWN = 0,
    // Session events
    AZURE_MSG_TYPE_SESSION_CREATED,
    AZURE_MSG_TYPE_SESSION_UPDATED,
    // Input audio events
    AZURE_MSG_TYPE_INPUT_AUDIO_BUFFER_SPEECH_STARTED,
    AZURE_MSG_TYPE_INPUT_AUDIO_BUFFER_SPEECH_STOPPED,
    AZURE_MSG_TYPE_INPUT_AUDIO_BUFFER_COMMITTED,
    // Response events
    AZURE_MSG_TYPE_RESPONSE_CREATED,
    AZURE_MSG_TYPE_RESPONSE_AUDIO_TRANSCRIPT_DELTA,
    AZURE_MSG_TYPE_RESPONSE_AUDIO_DELTA,
    AZURE_MSG_TYPE_RESPONSE_AUDIO_DONE,
    AZURE_MSG_TYPE_RESPONSE_DONE,
    // Error
    AZURE_MSG_TYPE_ERROR,
} azure_msg_type_t;

/**
 * @brief Azure event data (passed to callback)
 */
typedef struct {
    azure_msg_type_t type;
    const char *session_id;
    const char *item_id;
    const char *text;           // Text transcript
    const uint8_t *audio_data;  // Audio data (PCM16)
    size_t audio_size;          // Audio data size
    const char *error_message;  // Error message if type is ERROR
    int error_code;             // Error code
} azure_event_t;

/**
 * @brief Azure event callback function type
 *
 * @param event Event data
 * @param user_data User context
 */
typedef void (*azure_event_callback_t)(const azure_event_t *event, void *user_data);

/**
 * @brief Azure Realtime client configuration
 */
typedef struct {
    const char *api_key;            // Azure OpenAI API key
    const char *endpoint;           // Azure endpoint (e.g., "my-resource.openai.azure.com")
    const char *resource_name;      // DEPRECATED: Use endpoint instead
    const char *deployment_name;    // Deployment name (e.g., "gpt-realtime")
    const char *voice;              // Voice ID (alloy, echo, shimmer)
    uint32_t sample_rate;           // Audio sample rate (8000 for G.711)
    const char *audio_format;       // Audio format (g711_ulaw)
    bool use_server_vad;            // Use server VAD (true) or manual mode (false)
    azure_event_callback_t callback; // Event callback
    void *user_data;                // User context for callback
} azure_realtime_config_t;

// Default configuration
#define AZURE_REALTIME_DEFAULT_CONFIG() {       \
    .api_key = NULL,                            \
    .resource_name = NULL,                      \
    .deployment_name = "gpt-4o-realtime-preview", \
    .voice = "alloy",                           \
    .sample_rate = 8000,                        \
    .audio_format = "g711_ulaw",                \
    .use_server_vad = false,                    \
    .callback = NULL,                           \
    .user_data = NULL,                          \
}

// ============================================
// Azure Realtime Client Function Declarations
// ============================================

/**
 * @brief Initialize Azure Realtime WebSocket client
 *
 * @return ESP_OK on success
 */
esp_err_t azure_realtime_init(void);

/**
 * @brief Deinitialize Azure Realtime WebSocket client
 *
 * @return ESP_OK on success
 */
esp_err_t azure_realtime_deinit(void);

/**
 * @brief Configure Azure Realtime client
 *
 * @param config Client configuration
 * @return ESP_OK on success
 */
esp_err_t azure_realtime_configure(const azure_realtime_config_t *config);

/**
 * @brief Connect to Azure OpenAI Realtime server
 *
 * @return ESP_OK on success
 */
esp_err_t azure_realtime_connect(void);

/**
 * @brief Disconnect from Azure server
 *
 * @return ESP_OK on success
 */
esp_err_t azure_realtime_disconnect(void);

/**
 * @brief Check if connected to Azure server
 *
 * @return true if connected and ready
 */
bool azure_realtime_is_connected(void);

/**
 * @brief Get current connection state
 *
 * @return Current state
 */
azure_state_t azure_realtime_get_state(void);

/**
 * @brief Start a new conversation session
 *
 * @return ESP_OK on success
 */
esp_err_t azure_realtime_start_session(void);

/**
 * @brief End current conversation session
 *
 * @return ESP_OK on success
 */
esp_err_t azure_realtime_end_session(void);

/**
 * @brief Send audio data to Azure
 *
 * @param audio_data Audio data (PCM16 format, will be converted to G.711)
 * @param size Size in bytes
 * @return ESP_OK on success
 */
esp_err_t azure_realtime_send_audio(const uint8_t *audio_data, size_t size);

/**
 * @brief Commit audio buffer (signal end of user speech)
 *
 * @return ESP_OK on success
 */
esp_err_t azure_realtime_commit_audio(void);

/**
 * @brief Request response generation (required for manual mode)
 *
 * @return ESP_OK on success
 */
esp_err_t azure_realtime_create_response(void);

/**
 * @brief Cancel current response
 *
 * @return ESP_OK on success
 */
esp_err_t azure_realtime_cancel_response(void);

/**
 * @brief Register event callback
 *
 * @param callback Callback function
 * @param user_data User context
 * @return ESP_OK on success
 */
esp_err_t azure_realtime_register_callback(azure_event_callback_t callback, void *user_data);

/**
 * @brief Get current session ID
 *
 * @return Session ID string, or NULL if no session
 */
const char *azure_realtime_get_session_id(void);

/**
 * @brief Start Azure Realtime WebSocket task
 *
 * @return ESP_OK on success
 */
esp_err_t azure_realtime_start_task(void);

/**
 * @brief Stop Azure Realtime WebSocket task
 *
 * @return ESP_OK on success
 */
esp_err_t azure_realtime_stop_task(void);

/**
 * @brief Get message type string for debugging
 *
 * @param type Message type
 * @return String representation
 */
const char *azure_realtime_msg_type_to_string(azure_msg_type_t type);

/**
 * @brief Get state string for debugging
 *
 * @param state Client state
 * @return String representation
 */
const char *azure_realtime_state_to_string(azure_state_t state);

#ifdef __cplusplus
}
#endif
