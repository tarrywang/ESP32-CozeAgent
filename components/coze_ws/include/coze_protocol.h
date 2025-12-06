/**
 * @file coze_protocol.h
 * @brief Coze WebSocket protocol definitions
 *
 * Defines JSON message structures for Coze API communication.
 * Based on Coze real-time API protocol.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================
// Protocol Event Types (server -> client)
// Coze Audio Speech WebSocket API format
// ============================================

// Session events
#define COZE_EVENT_SPEECH_CREATED               "speech.created"
#define COZE_EVENT_SESSION_UPDATED              "session.updated"

// Input audio events
#define COZE_EVENT_INPUT_AUDIO_BUFFER_SPEECH_STARTED    "input_audio_buffer.speech_started"
#define COZE_EVENT_INPUT_AUDIO_BUFFER_SPEECH_STOPPED    "input_audio_buffer.speech_stopped"

// Conversation events (Coze protocol - NOT OpenAI format!)
// Coze uses "conversation.*" events, not "response.*"
#define COZE_EVENT_CONVERSATION_AUDIO_DELTA     "conversation.audio.delta"
#define COZE_EVENT_CONVERSATION_CHAT_COMPLETED  "conversation.chat.completed"
#define COZE_EVENT_CONVERSATION_CHAT_CANCELED   "conversation.chat.canceled"

// Error event
#define COZE_EVENT_ERROR                        "error"

// ============================================
// Protocol Command Types (client -> server)
// Coze Audio Speech WebSocket API format
// ============================================

// Supported commands for Coze Audio Speech WebSocket API:
// NOTE: Coze uses different event names than OpenAI Realtime API!
#define COZE_CMD_SESSION_UPDATE                 "session.update"
#define COZE_CMD_INPUT_AUDIO_BUFFER_APPEND      "input_audio_buffer.append"
#define COZE_CMD_INPUT_AUDIO_BUFFER_COMPLETE    "input_audio_buffer.complete"  // Coze uses "complete", NOT "commit"
#define COZE_CMD_INPUT_AUDIO_BUFFER_CLEAR       "input_audio_buffer.clear"
// NOTE: Coze does NOT use "response.create" - AI response auto-triggers after "complete"
// NOTE: conversation.item.create is NOT supported (causes 4000 error)

// ============================================
// Protocol Constants
// ============================================

#define COZE_MAX_SESSION_ID_LEN     64
#define COZE_MAX_CONVERSATION_ID_LEN 64
#define COZE_MAX_ITEM_ID_LEN        64
#define COZE_MAX_TEXT_LEN           4096
#define COZE_MAX_ERROR_MSG_LEN      256

// Audio chunk size for streaming (60ms of audio)
// NOTE: PCM16 frame size, will be encoded to G.711 μ-law (480 bytes) before sending
#define COZE_AUDIO_CHUNK_SIZE       960   // 8kHz * 16-bit * 60ms / 8 = 960 bytes PCM16

// ============================================
// Protocol Helper Functions
// ============================================

/**
 * @brief Build chat.update message to initialize chat session
 *
 * @param buffer Output buffer
 * @param size Buffer size
 * @param bot_id Bot ID
 * @param user_id User ID (optional, can be NULL)
 * @param conversation_id Conversation ID (optional, can be NULL for new conversation)
 * @return Length of message, or -1 on error
 */
int coze_protocol_build_chat_update(char *buffer, size_t size,
                                     const char *bot_id, const char *user_id,
                                     const char *conversation_id);

// NOTE: coze_protocol_build_message_create() removed - not supported by Audio Speech API

/**
 * @brief Build input_audio_buffer.append message
 *
 * @param buffer Output buffer
 * @param size Buffer size
 * @param audio_data Audio data (will be base64 encoded)
 * @param audio_size Audio data size
 * @return Length of message, or -1 on error
 */
int coze_protocol_build_audio_append(char *buffer, size_t size,
                                      const uint8_t *audio_data, size_t audio_size);

/**
 * @brief Build input_audio_buffer.complete message (Coze protocol)
 *
 * Signals end of user speech. In Coze, this automatically triggers AI response
 * generation (unlike OpenAI which requires separate response.create).
 *
 * @param buffer Output buffer
 * @param size Buffer size
 * @return Length of message, or -1 on error
 */
int coze_protocol_build_audio_complete(char *buffer, size_t size);

/**
 * @brief Parse event type from JSON message
 *
 * @param json_str JSON string
 * @param event_type Output buffer for event type
 * @param event_type_size Buffer size
 * @return true on success
 */
bool coze_protocol_parse_event_type(const char *json_str, char *event_type, size_t event_type_size);

/**
 * @brief Parse chat ID from chat.created message
 *
 * @param json_str JSON string
 * @param chat_id Output buffer
 * @param chat_id_size Buffer size
 * @return true on success
 */
bool coze_protocol_parse_chat_id(const char *json_str, char *chat_id, size_t chat_id_size);

/**
 * @brief Parse conversation ID from message
 *
 * @param json_str JSON string
 * @param conversation_id Output buffer
 * @param conversation_id_size Buffer size
 * @return true on success
 */
bool coze_protocol_parse_conversation_id(const char *json_str, char *conversation_id, size_t conversation_id_size);

/**
 * @brief Parse message delta from conversation.message.delta
 *
 * @param json_str JSON string
 * @param text Output buffer for text delta
 * @param text_size Buffer size
 * @param role Output buffer for role (assistant/user)
 * @param role_size Role buffer size
 * @return true on success
 */
bool coze_protocol_parse_message_delta(const char *json_str, char *text, size_t text_size,
                                        char *role, size_t role_size);

/**
 * @brief Parse audio delta from conversation.audio.delta
 *
 * @param json_str JSON string
 * @param audio_data Output buffer for decoded audio
 * @param audio_size Pointer to store decoded size
 * @param max_size Maximum output buffer size
 * @return true on success
 */
bool coze_protocol_parse_audio_delta(const char *json_str, uint8_t *audio_data,
                                      size_t *audio_size, size_t max_size);

/**
 * @brief Parse error from JSON message
 *
 * @param json_str JSON string
 * @param error_msg Output buffer for error message
 * @param error_msg_size Buffer size
 * @param error_code Pointer to store error code
 * @return true on success
 */
bool coze_protocol_parse_error(const char *json_str, char *error_msg,
                                size_t error_msg_size, int *error_code);

/**
 * @brief Base64 encode data
 *
 * @param src Source data
 * @param src_len Source length
 * @param dst Destination buffer
 * @param dst_size Destination buffer size
 * @return Encoded length, or -1 on error
 */
int coze_protocol_base64_encode(const uint8_t *src, size_t src_len,
                                 char *dst, size_t dst_size);

/**
 * @brief Base64 decode data
 *
 * @param src Source string
 * @param src_len Source length
 * @param dst Destination buffer
 * @param dst_size Destination buffer size
 * @return Decoded length, or -1 on error
 */
int coze_protocol_base64_decode(const char *src, size_t src_len,
                                 uint8_t *dst, size_t dst_size);

#ifdef __cplusplus
}
#endif
