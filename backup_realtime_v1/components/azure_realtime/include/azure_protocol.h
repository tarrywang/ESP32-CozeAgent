/**
 * @file azure_protocol.h
 * @brief Azure OpenAI Realtime API Protocol Definitions
 *
 * Based on OpenAI Realtime API specification
 * Reference: https://platform.openai.com/docs/guides/realtime
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================
// Azure OpenAI Realtime API Configuration
// ============================================

// API endpoint template (user must provide resource name and deployment)
// Format: wss://{resource}.openai.azure.com/openai/realtime?api-version=2024-10-01-preview&deployment={deployment}
#define AZURE_REALTIME_API_VERSION  "2024-10-01-preview"

// Authentication
#define AZURE_OPENAI_API_KEY        "YOUR_AZURE_OPENAI_API_KEY_HERE"
#define AZURE_OPENAI_RESOURCE       "anony-company"
#define AZURE_DEPLOYMENT_NAME       "gpt-realtime"

// Audio configuration (compatible with Coze's G.711)
#define AZURE_AUDIO_SAMPLE_RATE     8000
#define AZURE_AUDIO_FORMAT          "g711_ulaw"
#define AZURE_AUDIO_CHANNELS        1

// Voice options for Azure OpenAI
#define AZURE_VOICE_ALLOY           "alloy"
#define AZURE_VOICE_ECHO            "echo"
#define AZURE_VOICE_SHIMMER         "shimmer"
#define AZURE_DEFAULT_VOICE         AZURE_VOICE_ALLOY

// ============================================
// Server → Client Events (OpenAI Realtime API)
// ============================================

// Session events
#define AZURE_EVENT_SESSION_CREATED             "session.created"
#define AZURE_EVENT_SESSION_UPDATED             "session.updated"

// Input audio buffer events
#define AZURE_EVENT_INPUT_AUDIO_BUFFER_SPEECH_STARTED   "input_audio_buffer.speech_started"
#define AZURE_EVENT_INPUT_AUDIO_BUFFER_SPEECH_STOPPED   "input_audio_buffer.speech_stopped"
#define AZURE_EVENT_INPUT_AUDIO_BUFFER_COMMITTED        "input_audio_buffer.committed"
#define AZURE_EVENT_INPUT_AUDIO_BUFFER_CLEARED          "input_audio_buffer.cleared"

// Conversation events
#define AZURE_EVENT_CONVERSATION_CREATED        "conversation.created"
#define AZURE_EVENT_CONVERSATION_ITEM_CREATED   "conversation.item.created"

// Response events
#define AZURE_EVENT_RESPONSE_CREATED            "response.created"
#define AZURE_EVENT_RESPONSE_OUTPUT_ITEM_ADDED  "response.output_item.added"
#define AZURE_EVENT_RESPONSE_OUTPUT_ITEM_DONE   "response.output_item.done"
#define AZURE_EVENT_RESPONSE_CONTENT_PART_ADDED "response.content_part.added"
#define AZURE_EVENT_RESPONSE_CONTENT_PART_DONE  "response.content_part.done"

// Audio events (key events for voice conversation)
#define AZURE_EVENT_RESPONSE_AUDIO_TRANSCRIPT_DELTA     "response.audio_transcript.delta"
#define AZURE_EVENT_RESPONSE_AUDIO_TRANSCRIPT_DONE      "response.audio_transcript.done"
#define AZURE_EVENT_RESPONSE_AUDIO_DELTA                "response.audio.delta"
#define AZURE_EVENT_RESPONSE_AUDIO_DONE                 "response.audio.done"

// Response completion
#define AZURE_EVENT_RESPONSE_DONE               "response.done"

// Error event
#define AZURE_EVENT_ERROR                       "error"

// Rate limits event
#define AZURE_EVENT_RATE_LIMITS_UPDATED         "rate_limits.updated"

// ============================================
// Client → Server Commands
// ============================================

#define AZURE_CMD_SESSION_UPDATE                "session.update"
#define AZURE_CMD_INPUT_AUDIO_BUFFER_APPEND     "input_audio_buffer.append"
#define AZURE_CMD_INPUT_AUDIO_BUFFER_COMMIT     "input_audio_buffer.commit"
#define AZURE_CMD_INPUT_AUDIO_BUFFER_CLEAR      "input_audio_buffer.clear"
#define AZURE_CMD_CONVERSATION_ITEM_CREATE      "conversation.item.create"
#define AZURE_CMD_CONVERSATION_ITEM_TRUNCATE    "conversation.item.truncate"
#define AZURE_CMD_CONVERSATION_ITEM_DELETE      "conversation.item.delete"
#define AZURE_CMD_RESPONSE_CREATE               "response.create"
#define AZURE_CMD_RESPONSE_CANCEL               "response.cancel"

// ============================================
// Protocol Helper Functions
// ============================================

/**
 * @brief Build session.update command
 *
 * @param buffer Output buffer for JSON string
 * @param size Buffer size
 * @return Length of JSON string, or -1 on error
 */
int azure_protocol_build_session_update(char *buffer, size_t size);

/**
 * @brief Build input_audio_buffer.append command
 *
 * @param buffer Output buffer for JSON string
 * @param size Buffer size
 * @param audio_data Audio data (G.711 μ-law encoded)
 * @param audio_size Size of audio data in bytes
 * @return Length of JSON string, or -1 on error
 */
int azure_protocol_build_audio_append(char *buffer, size_t size,
                                       const uint8_t *audio_data, size_t audio_size);

/**
 * @brief Build input_audio_buffer.commit command
 *
 * @param buffer Output buffer for JSON string
 * @param size Buffer size
 * @return Length of JSON string, or -1 on error
 */
int azure_protocol_build_audio_commit(char *buffer, size_t size);

/**
 * @brief Build response.create command
 *
 * @param buffer Output buffer for JSON string
 * @param size Buffer size
 * @return Length of JSON string, or -1 on error
 */
int azure_protocol_build_response_create(char *buffer, size_t size);

/**
 * @brief Build response.cancel command
 *
 * @param buffer Output buffer for JSON string
 * @param size Buffer size
 * @return Length of JSON string, or -1 on error
 */
int azure_protocol_build_response_cancel(char *buffer, size_t size);

/**
 * @brief Parse event type from received message
 *
 * @param json_str JSON message string
 * @param event_type Output buffer for event type
 * @param event_type_size Size of event_type buffer
 * @return true if parsing succeeded
 */
bool azure_protocol_parse_event_type(const char *json_str,
                                      char *event_type, size_t event_type_size);

/**
 * @brief Parse audio delta from response.audio.delta event
 *
 * @param json_str JSON message string
 * @param audio_data Output buffer for decoded audio (PCM16)
 * @param audio_size Input: buffer size, Output: actual audio size
 * @return true if parsing succeeded
 */
bool azure_protocol_parse_audio_delta(const char *json_str,
                                       uint8_t *audio_data, size_t *audio_size);

/**
 * @brief Parse text transcript from response.audio_transcript.delta event
 *
 * @param json_str JSON message string
 * @param text Output buffer for text
 * @param text_size Size of text buffer
 * @return true if parsing succeeded
 */
bool azure_protocol_parse_transcript_delta(const char *json_str,
                                            char *text, size_t text_size);

/**
 * @brief Parse error message from error event
 *
 * @param json_str JSON message string
 * @param error_msg Output buffer for error message
 * @param error_msg_size Size of error_msg buffer
 * @param error_code Output for error code
 * @return true if parsing succeeded
 */
bool azure_protocol_parse_error(const char *json_str,
                                 char *error_msg, size_t error_msg_size,
                                 int *error_code);

// ============================================
// Base64 Encode/Decode (for audio data)
// ============================================

/**
 * @brief Base64 encode binary data
 *
 * @param src Source binary data
 * @param src_len Source data length
 * @param dst Destination buffer for Base64 string
 * @param dst_size Destination buffer size
 * @return Length of encoded string, or -1 on error
 */
int azure_protocol_base64_encode(const uint8_t *src, size_t src_len,
                                  char *dst, size_t dst_size);

/**
 * @brief Base64 decode string to binary data
 *
 * @param src Source Base64 string
 * @param src_len Source string length
 * @param dst Destination buffer for binary data
 * @param dst_size Destination buffer size
 * @return Length of decoded data, or -1 on error
 */
int azure_protocol_base64_decode(const char *src, size_t src_len,
                                  uint8_t *dst, size_t dst_size);

#ifdef __cplusplus
}
#endif
