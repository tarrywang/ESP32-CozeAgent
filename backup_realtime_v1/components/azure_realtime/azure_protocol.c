/**
 * @file azure_protocol.c
 * @brief Azure OpenAI Realtime API protocol implementation
 *
 * Based on OpenAI Realtime API specification
 * Adapted from coze_protocol.c for Azure compatibility
 */

#include "azure_protocol.h"

#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "cJSON.h"
#include "mbedtls/base64.h"

static const char *TAG = "AZURE_PROTOCOL";

// ============================================
// Base64 Encoding/Decoding
// (Copied from coze_protocol.c - uses mbedtls)
// ============================================

int azure_protocol_base64_encode(const uint8_t *src, size_t src_len,
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

int azure_protocol_base64_decode(const char *src, size_t src_len,
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
// Azure OpenAI Realtime API format
// ============================================

/**
 * Build session.update message for Azure OpenAI Realtime API
 * Format (OpenAI Realtime API):
 * {
 *   "type": "session.update",
 *   "session": {
 *     "modalities": ["text", "audio"],
 *     "voice": "alloy",
 *     "input_audio_format": "g711_ulaw",
 *     "output_audio_format": "g711_ulaw",
 *     "input_audio_transcription": {
 *       "model": "whisper-1"
 *     },
 *     "turn_detection": null  // Manual mode
 *   }
 * }
 * NOTE: Manual mode (turn_detection: null) requires explicit response.create
 */
int azure_protocol_build_session_update(char *buffer, size_t size)
{
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) return -1;

    cJSON_AddStringToObject(root, "type", "session.update");

    cJSON *session = cJSON_AddObjectToObject(root, "session");
    if (session) {
        // Modalities (text + audio)
        cJSON *modalities = cJSON_CreateArray();
        cJSON_AddItemToArray(modalities, cJSON_CreateString("text"));
        cJSON_AddItemToArray(modalities, cJSON_CreateString("audio"));
        cJSON_AddItemToObject(session, "modalities", modalities);

        // Voice (Azure OpenAI voices: alloy, echo, shimmer)
        cJSON_AddStringToObject(session, "voice", AZURE_DEFAULT_VOICE);

        // Audio formats (G.711 μ-law for bandwidth optimization)
        cJSON_AddStringToObject(session, "input_audio_format", AZURE_AUDIO_FORMAT);
        cJSON_AddStringToObject(session, "output_audio_format", AZURE_AUDIO_FORMAT);

        // Input audio transcription (optional - enables text transcript of user audio)
        cJSON *transcription = cJSON_AddObjectToObject(session, "input_audio_transcription");
        if (transcription) {
            cJSON_AddStringToObject(transcription, "model", "whisper-1");
        }

        // Turn detection - Manual mode (null = client controls flow)
        // For server VAD mode, use:
        // {
        //   "type": "server_vad",
        //   "threshold": 0.5,
        //   "prefix_padding_ms": 300,
        //   "silence_duration_ms": 500
        // }
        cJSON_AddNullToObject(session, "turn_detection");
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

/**
 * Build input_audio_buffer.append message
 * Format (same as Coze):
 * {
 *   "type": "input_audio_buffer.append",
 *   "audio": "<base64 encoded audio>"
 * }
 */
int azure_protocol_build_audio_append(char *buffer, size_t size,
                                       const uint8_t *audio_data, size_t audio_size)
{
    // Calculate required base64 size
    size_t base64_size = ((audio_size + 2) / 3) * 4 + 1;
    char *base64_data = malloc(base64_size);
    if (base64_data == NULL) return -1;

    int b64_len = azure_protocol_base64_encode(audio_data, audio_size, base64_data, base64_size);
    if (b64_len < 0) {
        free(base64_data);
        return -1;
    }

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        free(base64_data);
        return -1;
    }

    cJSON_AddStringToObject(root, "type", "input_audio_buffer.append");
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
 * Build input_audio_buffer.commit message (OpenAI Realtime API)
 * Format:
 * {
 *   "type": "input_audio_buffer.commit"
 * }
 * NOTE: OpenAI uses "commit" NOT "complete" (Coze uses "complete")
 * NOTE: In manual mode, this does NOT automatically trigger response
 *       Must call response.create separately
 */
int azure_protocol_build_audio_commit(char *buffer, size_t size)
{
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) return -1;

    // OpenAI protocol: "input_audio_buffer.commit" (NOT "complete")
    cJSON_AddStringToObject(root, "type", "input_audio_buffer.commit");

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
 * Build response.create message (required for manual mode)
 * Format:
 * {
 *   "type": "response.create",
 *   "response": {
 *     "modalities": ["text", "audio"],
 *     "instructions": ""  // Optional: override system instructions
 *   }
 * }
 * NOTE: Coze doesn't need this (auto-triggers after complete)
 *       Azure OpenAI requires explicit response.create in manual mode
 */
int azure_protocol_build_response_create(char *buffer, size_t size)
{
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) return -1;

    cJSON_AddStringToObject(root, "type", "response.create");

    cJSON *response = cJSON_AddObjectToObject(root, "response");
    if (response) {
        // Modalities for response (text + audio)
        cJSON *modalities = cJSON_CreateArray();
        cJSON_AddItemToArray(modalities, cJSON_CreateString("text"));
        cJSON_AddItemToArray(modalities, cJSON_CreateString("audio"));
        cJSON_AddItemToObject(response, "modalities", modalities);
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

/**
 * Build response.cancel message
 * Format:
 * {
 *   "type": "response.cancel"
 * }
 */
int azure_protocol_build_response_cancel(char *buffer, size_t size)
{
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) return -1;

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
// ============================================

/**
 * Parse event type from received JSON message
 */
bool azure_protocol_parse_event_type(const char *json_str,
                                      char *event_type, size_t event_type_size)
{
    cJSON *root = cJSON_Parse(json_str);
    if (root == NULL) return false;

    cJSON *type = cJSON_GetObjectItem(root, "type");
    if (type == NULL || !cJSON_IsString(type)) {
        cJSON_Delete(root);
        return false;
    }

    strncpy(event_type, type->valuestring, event_type_size - 1);
    event_type[event_type_size - 1] = '\0';

    cJSON_Delete(root);
    return true;
}

/**
 * Parse audio delta from response.audio.delta event
 * Format:
 * {
 *   "type": "response.audio.delta",
 *   "delta": "<base64 encoded audio>"
 * }
 * NOTE: Audio is G.711 μ-law encoded, needs to be decoded to PCM16 for playback
 */
bool azure_protocol_parse_audio_delta(const char *json_str,
                                       uint8_t *audio_data, size_t *audio_size)
{
    cJSON *root = cJSON_Parse(json_str);
    if (root == NULL) return false;

    cJSON *delta = cJSON_GetObjectItem(root, "delta");
    if (delta == NULL || !cJSON_IsString(delta)) {
        cJSON_Delete(root);
        return false;
    }

    // Decode base64 audio data
    int decoded_len = azure_protocol_base64_decode(delta->valuestring,
                                                     strlen(delta->valuestring),
                                                     audio_data, *audio_size);
    cJSON_Delete(root);

    if (decoded_len < 0) return false;

    *audio_size = decoded_len;
    return true;
}

/**
 * Parse text transcript from response.audio_transcript.delta event
 * Format:
 * {
 *   "type": "response.audio_transcript.delta",
 *   "delta": "text chunk"
 * }
 */
bool azure_protocol_parse_transcript_delta(const char *json_str,
                                            char *text, size_t text_size)
{
    cJSON *root = cJSON_Parse(json_str);
    if (root == NULL) return false;

    cJSON *delta = cJSON_GetObjectItem(root, "delta");
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
 * Parse error message from error event
 * Format:
 * {
 *   "type": "error",
 *   "error": {
 *     "message": "error description",
 *     "code": "error_code"
 *   }
 * }
 */
bool azure_protocol_parse_error(const char *json_str,
                                 char *error_msg, size_t error_msg_size,
                                 int *error_code)
{
    cJSON *root = cJSON_Parse(json_str);
    if (root == NULL) return false;

    cJSON *error = cJSON_GetObjectItem(root, "error");
    if (error == NULL) {
        cJSON_Delete(root);
        return false;
    }

    cJSON *message = cJSON_GetObjectItem(error, "message");
    if (message && cJSON_IsString(message)) {
        strncpy(error_msg, message->valuestring, error_msg_size - 1);
        error_msg[error_msg_size - 1] = '\0';
    }

    cJSON *code = cJSON_GetObjectItem(error, "code");
    if (code && cJSON_IsString(code)) {
        // Try to parse code as string or number
        *error_code = 0;  // Default error code
    }

    cJSON_Delete(root);
    return true;
}
