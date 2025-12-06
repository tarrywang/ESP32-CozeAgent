/**
 * @file coze_ws.h
 * @brief Coze AI WebSocket client interface
 *
 * Implements bidirectional communication with Coze AI Agent
 * for voice-based conversations.
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
// Coze API Configuration
// ============================================

// API endpoint (from Coze SDK: wss://ws.coze.com for international, wss://ws.coze.cn for China)
// Using Audio Speech WebSocket endpoint for voice conversation
#define COZE_WS_HOST            "wss://ws.coze.cn"
#define COZE_WS_PATH            "/v1/audio/speech"

// Authentication credentials
#define COZE_API_TOKEN          "pat_03sEeuO3giNxZqReCswNKKfrtTDLO8HHXD01KtNJT9TVnA1Txx8VVeYRFdzjjkMk"
#define COZE_BOT_ID             "7565482471721353254"
#define COZE_USER_ID            "esp32-tarrydevice"     // User ID for session identification
#define COZE_VOICE_ID           "7426720361733046281"  // Default Chinese female voice

// Audio configuration (G.711 μ-law for TLS bandwidth optimization)
#define COZE_AUDIO_SAMPLE_RATE  8000    // 8kHz for G.711 narrowband
#define COZE_AUDIO_FORMAT       "g711_ulaw"  // G.711 μ-law compression (2:1 ratio)
#define COZE_AUDIO_CHANNELS     1       // Mono

// ============================================
// Coze Client State
// ============================================

/**
 * @brief Coze WebSocket connection state
 */
typedef enum {
    COZE_STATE_DISCONNECTED = 0,
    COZE_STATE_CONNECTING,
    COZE_STATE_CONNECTED,
    COZE_STATE_AUTHENTICATING,
    COZE_STATE_READY,
    COZE_STATE_STREAMING,
    COZE_STATE_ERROR,
} coze_state_t;

/**
 * @brief Coze message types (Audio Speech WebSocket API)
 */
typedef enum {
    COZE_MSG_TYPE_UNKNOWN = 0,
    // Session events
    COZE_MSG_TYPE_SPEECH_CREATED,
    COZE_MSG_TYPE_SESSION_UPDATED,
    // Input audio events
    COZE_MSG_TYPE_INPUT_AUDIO_BUFFER_SPEECH_STARTED,
    COZE_MSG_TYPE_INPUT_AUDIO_BUFFER_SPEECH_STOPPED,
    // Response events
    COZE_MSG_TYPE_RESPONSE_CREATED,
    COZE_MSG_TYPE_RESPONSE_AUDIO_TRANSCRIPT_DELTA,
    COZE_MSG_TYPE_RESPONSE_AUDIO_DELTA,
    COZE_MSG_TYPE_RESPONSE_AUDIO_DONE,
    COZE_MSG_TYPE_RESPONSE_DONE,
    // Error
    COZE_MSG_TYPE_ERROR,
} coze_msg_type_t;

/**
 * @brief Coze event data
 */
typedef struct {
    coze_msg_type_t type;
    const char *session_id;
    const char *conversation_id;
    const char *item_id;
    const char *text;           // Text transcript
    const uint8_t *audio_data;  // Audio data (PCM)
    size_t audio_size;          // Audio data size
    const char *error_message;  // Error message if type is ERROR
    int error_code;             // Error code
} coze_event_t;

/**
 * @brief Coze event callback function type
 *
 * @param event Event data
 * @param user_data User context
 */
typedef void (*coze_event_callback_t)(const coze_event_t *event, void *user_data);

/**
 * @brief Coze client configuration
 */
typedef struct {
    const char *api_token;          // API authentication token
    const char *bot_id;             // Bot ID to interact with
    const char *voice_id;           // Voice ID for TTS
    uint32_t sample_rate;           // Audio sample rate
    const char *audio_format;       // Audio format (pcm, opus, etc.)
    coze_event_callback_t callback; // Event callback
    void *user_data;                // User context for callback
} coze_ws_config_t;

// Default configuration
#define COZE_WS_DEFAULT_CONFIG() {     \
    .api_token = COZE_API_TOKEN,       \
    .bot_id = COZE_BOT_ID,             \
    .voice_id = COZE_VOICE_ID,         \
    .sample_rate = COZE_AUDIO_SAMPLE_RATE, \
    .audio_format = COZE_AUDIO_FORMAT, \
    .callback = NULL,                  \
    .user_data = NULL,                 \
}

// ============================================
// Coze Client Function Declarations
// ============================================

/**
 * @brief Initialize Coze WebSocket client
 *
 * @return ESP_OK on success
 */
esp_err_t coze_ws_init(void);

/**
 * @brief Deinitialize Coze WebSocket client
 *
 * @return ESP_OK on success
 */
esp_err_t coze_ws_deinit(void);

/**
 * @brief Configure Coze client
 *
 * @param config Client configuration
 * @return ESP_OK on success
 */
esp_err_t coze_ws_configure(const coze_ws_config_t *config);

/**
 * @brief Connect to Coze server
 *
 * @return ESP_OK on success
 */
esp_err_t coze_ws_connect(void);

/**
 * @brief Disconnect from Coze server
 *
 * @return ESP_OK on success
 */
esp_err_t coze_ws_disconnect(void);

/**
 * @brief Check if connected to Coze server
 *
 * @return true if connected and ready
 */
bool coze_ws_is_connected(void);

/**
 * @brief Get current connection state
 *
 * @return Current state
 */
coze_state_t coze_ws_get_state(void);

/**
 * @brief Start a new conversation session
 *
 * @return ESP_OK on success
 */
esp_err_t coze_ws_start_session(void);

/**
 * @brief End current conversation session
 *
 * @return ESP_OK on success
 */
esp_err_t coze_ws_end_session(void);

/**
 * @brief Send audio data to Coze
 *
 * @param audio_data Audio data (PCM format)
 * @param size Size in bytes
 * @return ESP_OK on success
 */
esp_err_t coze_ws_send_audio(const uint8_t *audio_data, size_t size);

/**
 * @brief Send text message to Coze
 *
 * @param text Text message
 * @return ESP_OK on success
 */
esp_err_t coze_ws_send_text(const char *text);

/**
 * @brief Commit audio buffer (signal end of user speech)
 *
 * @return ESP_OK on success
 */
esp_err_t coze_ws_commit_audio(void);

/**
 * @brief Cancel current response
 *
 * @return ESP_OK on success
 */
esp_err_t coze_ws_cancel_response(void);

/**
 * @brief Request response generation
 *
 * @return ESP_OK on success
 */
esp_err_t coze_ws_create_response(void);

/**
 * @brief Register event callback
 *
 * @param callback Callback function
 * @param user_data User context
 * @return ESP_OK on success
 */
esp_err_t coze_ws_register_callback(coze_event_callback_t callback, void *user_data);

/**
 * @brief Get current session ID
 *
 * @return Session ID string, or NULL if no session
 */
const char *coze_ws_get_session_id(void);

/**
 * @brief Start Coze WebSocket task
 *
 * @return ESP_OK on success
 */
esp_err_t coze_ws_start_task(void);

/**
 * @brief Stop Coze WebSocket task
 *
 * @return ESP_OK on success
 */
esp_err_t coze_ws_stop_task(void);

/**
 * @brief Get message type string for debugging
 *
 * @param type Message type
 * @return String representation
 */
const char *coze_ws_msg_type_to_string(coze_msg_type_t type);

/**
 * @brief Get state string for debugging
 *
 * @param state Client state
 * @return String representation
 */
const char *coze_ws_state_to_string(coze_state_t state);

#ifdef __cplusplus
}
#endif
