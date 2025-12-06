/**
 * @file coze_protocol.c
 * @brief Coze WebSocket protocol implementation (Audio Speech API)
 */

#include "coze_protocol.h"
#include "coze_ws.h"

#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "cJSON.h"
#include "mbedtls/base64.h"

static const char *TAG = "COZE_PROTOCOL";

// ============================================
// Base64 Encoding/Decoding
// ============================================

int coze_protocol_base64_encode(const uint8_t *src, size_t src_len,
                                 char *dst, size_t dst_size)
{
    size_t olen = 0;
    int ret = mbedtls_base64_encode((unsigned char *)dst, dst_size, &olen, src, src_len);
    if (ret != 0) {
        ESP_LOGE(TAG, "Base64 encode failed: %d", ret);
        return -1;
    }
    return (int)olen;
}

int coze_protocol_base64_decode(const char *src, size_t src_len,
                                 uint8_t *dst, size_t dst_size)
{
    size_t olen = 0;
    int ret = mbedtls_base64_decode(dst, dst_size, &olen,
                                     (const unsigned char *)src, src_len);
    if (ret != 0) {
        ESP_LOGE(TAG, "Base64 decode failed: %d", ret);
        return -1;
    }
    return (int)olen;
}

// ============================================
// Message Building Functions
// Coze Audio Speech WebSocket API format
// ============================================

/**
 * Build session.update message to configure audio session
 * Format (Coze Realtime WebSocket API - Manual Mode):
 * {
 *   "type": "session.update",
 *   "session": {
 *     "bot_id": "xxx",
 *     "voice": "voice_id",
 *     "input_audio_format": {
 *       "type": "raw",
 *       "format": "pcm16",
 *       "sample_rate": 16000,
 *       "channels": 1
 *     },
 *     "output_audio_format": {
 *       "type": "raw",
 *       "format": "pcm16",
 *       "sample_rate": 16000,
 *       "channels": 1
 *     }
 *   }
 * }
 * NOTE: No turn_detection configured = Manual mode
 * Client controls flow with commit + response.create
 * IMPORTANT: type="raw" + format="pcm16", NOT type="pcm16"
 */
int coze_protocol_build_chat_update(char *buffer, size_t size,
                                     const char *bot_id, const char *user_id,
                                     const char *conversation_id)
{
    (void)conversation_id;  // Unused in current format

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) return -1;

    // NEW FORMAT: "type" instead of "event_type"
    cJSON_AddStringToObject(root, "type", "session.update");

    // NEW FORMAT: "session" instead of "data"
    cJSON *session = cJSON_AddObjectToObject(root, "session");
    if (session) {
        // Bot ID
        if (bot_id && strlen(bot_id) > 0) {
            cJSON_AddStringToObject(session, "bot_id", bot_id);
        }

        // User ID (for session identification and conversation tracking)
        if (user_id && strlen(user_id) > 0) {
            cJSON_AddStringToObject(session, "user_id", user_id);
        }

        // Voice ID (at session level in new format)
        cJSON_AddStringToObject(session, "voice", COZE_VOICE_ID);

        // Input audio format (G.711 μ-law for TLS bandwidth optimization)
        cJSON *input_audio_format = cJSON_AddObjectToObject(session, "input_audio_format");
        if (input_audio_format) {
            cJSON_AddStringToObject(input_audio_format, "type", "raw");
            cJSON_AddStringToObject(input_audio_format, "format", COZE_AUDIO_FORMAT);  // "g711_ulaw"
            cJSON_AddNumberToObject(input_audio_format, "sample_rate", COZE_AUDIO_SAMPLE_RATE);  // 8000 Hz
            cJSON_AddNumberToObject(input_audio_format, "channels", COZE_AUDIO_CHANNELS);
        }

        // Output audio format (G.711 μ-law)
        cJSON *output_audio_format = cJSON_AddObjectToObject(session, "output_audio_format");
        if (output_audio_format) {
            cJSON_AddStringToObject(output_audio_format, "type", "raw");
            cJSON_AddStringToObject(output_audio_format, "format", COZE_AUDIO_FORMAT);  // "g711_ulaw"
            cJSON_AddNumberToObject(output_audio_format, "sample_rate", COZE_AUDIO_SAMPLE_RATE);  // 8000 Hz
            cJSON_AddNumberToObject(output_audio_format, "channels", COZE_AUDIO_CHANNELS);
        }

        // Manual mode: No turn_detection configured
        // This allows client to control conversation flow with:
        // - input_audio_buffer.complete (signal end of user speech)
        // In Coze, sending "complete" automatically triggers AI response (no separate response.create needed)
        // Server VAD mode (turn_detection.type="server_vad") conflicts with manual mode
    }

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (json_str == NULL) return -1;

    int len = strlen(json_str);
    if (len >= size) {
        free(json_str);
        return -1;
    }

    strcpy(buffer, json_str);
    free(json_str);

    return len;
}

// NOTE: conversation.item.create is NOT supported by Audio Speech WebSocket API
// Removed: coze_protocol_build_message_create() function
// Use voice input instead of text input in pure audio mode

/**
 * Build input_audio_buffer.append message
 * NEW Format:
 * {
 *   "type": "input_audio_buffer.append",
 *   "audio": "<base64 encoded audio>"
 * }
 * NOTE: Uses "type" NOT "event_type", audio at root level NOT in "data"
 */
int coze_protocol_build_audio_append(char *buffer, size_t size,
                                      const uint8_t *audio_data, size_t audio_size)
{
    // Calculate required base64 size
    size_t base64_size = ((audio_size + 2) / 3) * 4 + 1;
    char *base64_data = malloc(base64_size);
    if (base64_data == NULL) return -1;

    int b64_len = coze_protocol_base64_encode(audio_data, audio_size, base64_data, base64_size);
    if (b64_len < 0) {
        free(base64_data);
        return -1;
    }

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        free(base64_data);
        return -1;
    }

    // NEW FORMAT: "type" instead of "event_type"
    cJSON_AddStringToObject(root, "type", "input_audio_buffer.append");
    // NEW FORMAT: "audio" at root level, not in "data" object
    cJSON_AddStringToObject(root, "audio", base64_data);

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    free(base64_data);

    if (json_str == NULL) return -1;

    int len = strlen(json_str);
    if (len >= size) {
        free(json_str);
        return -1;
    }

    strcpy(buffer, json_str);
    free(json_str);

    return len;
}

/**
 * Build input_audio_buffer.complete message (Coze protocol)
 * Format:
 * {
 *   "type": "input_audio_buffer.complete"
 * }
 * NOTE: Coze uses "complete" NOT "commit" (OpenAI uses "commit")
 * NOTE: In Coze, this automatically triggers AI response generation
 */
int coze_protocol_build_audio_complete(char *buffer, size_t size)
{
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) return -1;

    // Coze protocol: "input_audio_buffer.complete" (NOT "commit")
    cJSON_AddStringToObject(root, "type", "input_audio_buffer.complete");

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (json_str == NULL) return -1;

    int len = strlen(json_str);
    if (len >= size) {
        free(json_str);
        return -1;
    }

    strcpy(buffer, json_str);
    free(json_str);

    return len;
}

/**
 * Build response.cancel message
 * NEW Format:
 * {
 *   "type": "response.cancel"
 * }
 * NOTE: Uses "type" NOT "event_type"
 */
int coze_protocol_build_chat_cancel(char *buffer, size_t size)
{
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) return -1;

    // NEW FORMAT: "type" instead of "event_type"
    cJSON_AddStringToObject(root, "type", "response.cancel");

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (json_str == NULL) return -1;

    int len = strlen(json_str);
    if (len >= size) {
        free(json_str);
        return -1;
    }

    strcpy(buffer, json_str);
    free(json_str);

    return len;
}

// ============================================
// Message Parsing Functions
// Coze Audio Speech WebSocket API format
// ============================================

bool coze_protocol_parse_event_type(const char *json_str, char *event_type, size_t event_type_size)
{
    cJSON *root = cJSON_Parse(json_str);
    if (root == NULL) {
        const char *error_ptr = cJSON_GetErrorPtr();
        if (error_ptr != NULL) {
            ESP_LOGE(TAG, "JSON parse error before: %s", error_ptr);
        }
        ESP_LOGE(TAG, "Raw JSON: %s", json_str);
        return false;
    }

    // Try "event_type" first (Coze Chat WebSocket format)
    cJSON *type = cJSON_GetObjectItem(root, "event_type");

    // If not found, try "type" (Coze Audio Speech WebSocket / OpenAI Realtime format)
    if (type == NULL || !cJSON_IsString(type)) {
        type = cJSON_GetObjectItem(root, "type");
    }

    if (type == NULL || !cJSON_IsString(type)) {
        ESP_LOGE(TAG, "No 'event_type' or 'type' field found in JSON: %s", json_str);

        // DEBUG: Print all keys in the JSON
        cJSON *item = root->child;
        ESP_LOGW(TAG, "Available fields:");
        while (item != NULL) {
            ESP_LOGW(TAG, "  - %s", item->string ? item->string : "(null)");
            item = item->next;
        }

        cJSON_Delete(root);
        return false;
    }

    strncpy(event_type, type->valuestring, event_type_size - 1);
    event_type[event_type_size - 1] = '\0';

    cJSON_Delete(root);
    return true;
}

/**
 * Parse session ID from speech.created message
 * Old format: {"event_type":"speech.created","data":{"id":"xxx",...}}
 * New format: {"type":"speech.created","id":"xxx",...} or {"type":"speech.created","speech":{"id":"xxx",...}}
 */
bool coze_protocol_parse_chat_id(const char *json_str, char *chat_id, size_t chat_id_size)
{
    cJSON *root = cJSON_Parse(json_str);
    if (root == NULL) return false;

    cJSON *id = NULL;

    // Try root level "id" first (new format)
    id = cJSON_GetObjectItem(root, "id");

    // Try "data.id" (old format)
    if (id == NULL || !cJSON_IsString(id)) {
        cJSON *data = cJSON_GetObjectItem(root, "data");
        if (data != NULL) {
            id = cJSON_GetObjectItem(data, "id");
        }
    }

    // Try "speech.id" (alternative new format)
    if (id == NULL || !cJSON_IsString(id)) {
        cJSON *speech = cJSON_GetObjectItem(root, "speech");
        if (speech != NULL) {
            id = cJSON_GetObjectItem(speech, "id");
        }
    }

    if (id == NULL || !cJSON_IsString(id)) {
        cJSON_Delete(root);
        return false;
    }

    strncpy(chat_id, id->valuestring, chat_id_size - 1);
    chat_id[chat_id_size - 1] = '\0';

    cJSON_Delete(root);
    return true;
}

/**
 * Parse conversation ID from message
 * Format: {"event_type":"xxx","data":{"conversation_id":"xxx",...}}
 */
bool coze_protocol_parse_conversation_id(const char *json_str, char *conversation_id, size_t conversation_id_size)
{
    cJSON *root = cJSON_Parse(json_str);
    if (root == NULL) return false;

    cJSON *data = cJSON_GetObjectItem(root, "data");
    if (data == NULL) {
        cJSON_Delete(root);
        return false;
    }

    cJSON *conv_id = cJSON_GetObjectItem(data, "conversation_id");
    if (conv_id == NULL || !cJSON_IsString(conv_id)) {
        cJSON_Delete(root);
        return false;
    }

    strncpy(conversation_id, conv_id->valuestring, conversation_id_size - 1);
    conversation_id[conversation_id_size - 1] = '\0';

    cJSON_Delete(root);
    return true;
}

/**
 * Parse transcript delta from response.audio_transcript.delta
 * Old format: {"event_type":"response.audio_transcript.delta","data":{"delta":"xxx"}}
 * New format: {"type":"response.audio_transcript.delta","delta":"xxx"}
 */
bool coze_protocol_parse_message_delta(const char *json_str, char *text, size_t text_size,
                                        char *role, size_t role_size)
{
    cJSON *root = cJSON_Parse(json_str);
    if (root == NULL) return false;

    // Set role to "assistant" for audio responses
    if (role && role_size > 0) {
        strncpy(role, "assistant", role_size - 1);
        role[role_size - 1] = '\0';
    }

    cJSON *delta = NULL;

    // Try root level "delta" first (new format)
    delta = cJSON_GetObjectItem(root, "delta");

    // Try root level "transcript" (alternative new format)
    if (delta == NULL || !cJSON_IsString(delta)) {
        delta = cJSON_GetObjectItem(root, "transcript");
    }

    // Try "data.delta" (old format)
    if (delta == NULL || !cJSON_IsString(delta)) {
        cJSON *data = cJSON_GetObjectItem(root, "data");
        if (data != NULL) {
            delta = cJSON_GetObjectItem(data, "delta");
            if (delta == NULL || !cJSON_IsString(delta)) {
                delta = cJSON_GetObjectItem(data, "transcript");
            }
        }
    }

    if (delta == NULL || !cJSON_IsString(delta)) {
        cJSON_Delete(root);
        return false;
    }

    strncpy(text, delta->valuestring, text_size - 1);
    text[text_size - 1] = '\0';

    cJSON_Delete(root);
    return true;
}

/**
 * Parse audio delta from response.audio.delta
 * Old format: {"event_type":"response.audio.delta","data":{"delta":"<base64>"}}
 * New format: {"type":"response.audio.delta","delta":"<base64>"}
 */
bool coze_protocol_parse_audio_delta(const char *json_str, uint8_t *audio_data,
                                      size_t *audio_size, size_t max_size)
{
    cJSON *root = cJSON_Parse(json_str);
    if (root == NULL) return false;

    cJSON *delta = NULL;

    // Try root level "delta" first (new format)
    delta = cJSON_GetObjectItem(root, "delta");

    // Try root level "audio" (alternative new format)
    if (delta == NULL || !cJSON_IsString(delta)) {
        delta = cJSON_GetObjectItem(root, "audio");
    }

    // Try "data.delta" (old format)
    if (delta == NULL || !cJSON_IsString(delta)) {
        cJSON *data = cJSON_GetObjectItem(root, "data");
        if (data != NULL) {
            delta = cJSON_GetObjectItem(data, "delta");
            if (delta == NULL || !cJSON_IsString(delta)) {
                delta = cJSON_GetObjectItem(data, "audio");
            }
        }
    }

    if (delta == NULL || !cJSON_IsString(delta)) {
        cJSON_Delete(root);
        return false;
    }

    const char *base64_audio = delta->valuestring;
    size_t base64_len = strlen(base64_audio);

    int decoded_len = coze_protocol_base64_decode(base64_audio, base64_len,
                                                   audio_data, max_size);
    cJSON_Delete(root);

    if (decoded_len < 0) {
        return false;
    }

    *audio_size = (size_t)decoded_len;
    return true;
}

/**
 * Parse error from error event
 * Format: {"event_type":"error","code":4000,"msg":"xxx"}
 * or: {"event_type":"error","data":{"code":4000,"message":"xxx"}}
 */
bool coze_protocol_parse_error(const char *json_str, char *error_msg,
                                size_t error_msg_size, int *error_code)
{
    cJSON *root = cJSON_Parse(json_str);
    if (root == NULL) return false;

    // Try flat format first: {"event_type":"error","code":4000,"msg":"xxx"}
    cJSON *code = cJSON_GetObjectItem(root, "code");
    cJSON *msg = cJSON_GetObjectItem(root, "msg");

    if (code && cJSON_IsNumber(code)) {
        *error_code = code->valueint;
    } else {
        *error_code = -1;
    }

    if (msg && cJSON_IsString(msg)) {
        strncpy(error_msg, msg->valuestring, error_msg_size - 1);
        error_msg[error_msg_size - 1] = '\0';
        cJSON_Delete(root);
        return true;
    }

    // Try nested format: {"event_type":"error","data":{"code":4000,"message":"xxx"}}
    cJSON *data = cJSON_GetObjectItem(root, "data");
    if (data) {
        cJSON *data_code = cJSON_GetObjectItem(data, "code");
        cJSON *data_msg = cJSON_GetObjectItem(data, "message");

        if (data_code && cJSON_IsNumber(data_code)) {
            *error_code = data_code->valueint;
        }

        if (data_msg && cJSON_IsString(data_msg)) {
            strncpy(error_msg, data_msg->valuestring, error_msg_size - 1);
            error_msg[error_msg_size - 1] = '\0';
            cJSON_Delete(root);
            return true;
        }
    }

    strcpy(error_msg, "Unknown error");
    cJSON_Delete(root);
    return true;
}
