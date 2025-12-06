/**
 * @file azure_realtime.c
 * @brief Azure OpenAI Realtime API WebSocket client implementation
 *
 * Based on coze_ws.c architecture, adapted for Azure OpenAI Realtime API
 */

#include "azure_realtime.h"
#include "azure_protocol.h"

#include <string.h>
#include "esp_log.h"
#include "cJSON.h"
#include "esp_websocket_client.h"
#include "esp_crt_bundle.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

static const char *TAG = "AZURE_RT";

// ============================================
// Configuration Constants
// ============================================

#define AZURE_AUDIO_CHUNK_SIZE  960    // 60ms @ 8kHz × 2 bytes = 960 bytes per chunk
#define AUDIO_QUEUE_SIZE        20     // Buffer 20 chunks (~1.2 seconds)
#define AUDIO_BATCH_FRAMES      2      // Send 2 frames (~120ms) at a time
#define AUDIO_BATCH_TIMEOUT_MS  100    // Or timeout after 100ms
#define RECONNECT_DELAY_MS      5000   // 5 second delay before reconnection
#define WS_BUFFER_SIZE          8192   // WebSocket send buffer size

// ============================================
// Audio Chunk Structure
// ============================================

typedef struct {
    uint8_t data[AZURE_AUDIO_CHUNK_SIZE];
    size_t size;
} audio_chunk_t;

// ============================================
// Static Variables
// ============================================

static esp_websocket_client_handle_t s_ws_client = NULL;
static azure_realtime_config_t s_config = {0};
static azure_state_t s_state = AZURE_STATE_DISCONNECTED;
static QueueHandle_t s_audio_queue = NULL;
static TaskHandle_t s_task_handle = NULL;
static volatile bool s_task_running = false;
static volatile bool s_ws_cleanup_needed = false;
static volatile bool s_session_update_pending = false;

// Session ID (received from session.created event)
static char s_session_id[64] = {0};

// Statistics
static uint32_t s_send_count = 0;
static uint32_t s_recv_count = 0;

// ============================================
// G.711 μ-law Encode/Decode
// (Copied from coze_ws.c)
// ============================================

/**
 * @brief G.711 μ-law expansion table (8-bit → 16-bit PCM)
 * Converts μ-law compressed samples back to linear PCM16
 */
static const int16_t ulaw_exp_table[256] = {
    -32124,-31100,-30076,-29052,-28028,-27004,-25980,-24956,
    -23932,-22908,-21884,-20860,-19836,-18812,-17788,-16764,
    -15996,-15484,-14972,-14460,-13948,-13436,-12924,-12412,
    -11900,-11388,-10876,-10364, -9852, -9340, -8828, -8316,
     -7932, -7676, -7420, -7164, -6908, -6652, -6396, -6140,
     -5884, -5628, -5372, -5116, -4860, -4604, -4348, -4092,
     -3900, -3772, -3644, -3516, -3388, -3260, -3132, -3004,
     -2876, -2748, -2620, -2492, -2364, -2236, -2108, -1980,
     -1884, -1820, -1756, -1692, -1628, -1564, -1500, -1436,
     -1372, -1308, -1244, -1180, -1116, -1052,  -988,  -924,
      -876,  -844,  -812,  -780,  -748,  -716,  -684,  -652,
      -620,  -588,  -556,  -524,  -492,  -460,  -428,  -396,
      -372,  -356,  -340,  -324,  -308,  -292,  -276,  -260,
      -244,  -228,  -212,  -196,  -180,  -164,  -148,  -132,
      -120,  -112,  -104,   -96,   -88,   -80,   -72,   -64,
       -56,   -48,   -40,   -32,   -24,   -16,    -8,     0,
     32124, 31100, 30076, 29052, 28028, 27004, 25980, 24956,
     23932, 22908, 21884, 20860, 19836, 18812, 17788, 16764,
     15996, 15484, 14972, 14460, 13948, 13436, 12924, 12412,
     11900, 11388, 10876, 10364,  9852,  9340,  8828,  8316,
      7932,  7676,  7420,  7164,  6908,  6652,  6396,  6140,
      5884,  5628,  5372,  5116,  4860,  4604,  4348,  4092,
      3900,  3772,  3644,  3516,  3388,  3260,  3132,  3004,
      2876,  2748,  2620,  2492,  2364,  2236,  2108,  1980,
      1884,  1820,  1756,  1692,  1628,  1564,  1500,  1436,
      1372,  1308,  1244,  1180,  1116,  1052,   988,   924,
       876,   844,   812,   780,   748,   716,   684,   652,
       620,   588,   556,   524,   492,   460,   428,   396,
       372,   356,   340,   324,   308,   292,   276,   260,
       244,   228,   212,   196,   180,   164,   148,   132,
       120,   112,   104,    96,    88,    80,    72,    64,
        56,    48,    40,    32,    24,    16,     8,     0
};

/**
 * @brief Convert PCM16 sample to G.711 μ-law
 *
 * @param pcm 16-bit PCM sample
 * @return 8-bit μ-law encoded sample
 */
static inline uint8_t linear_to_ulaw(int16_t pcm)
{
    const int16_t BIAS = 0x84;
    const int16_t CLIP = 32635;

    int16_t sign = (pcm < 0) ? 0x80 : 0x00;
    int16_t sample = (pcm < 0) ? -pcm : pcm;

    if (sample > CLIP) sample = CLIP;
    sample += BIAS;

    int16_t exponent = 7;
    for (int16_t exp_mask = 0x4000; (sample & exp_mask) == 0 && exponent > 0; exp_mask >>= 1, exponent--);

    int16_t mantissa = (sample >> (exponent + 3)) & 0x0F;
    uint8_t ulaw = ~(sign | (exponent << 4) | mantissa);

    return ulaw;
}

/**
 * @brief Convert G.711 μ-law sample to PCM16
 *
 * @param ulaw 8-bit μ-law sample
 * @return 16-bit PCM sample
 */
static inline int16_t ulaw_to_linear(uint8_t ulaw)
{
    return ulaw_exp_table[ulaw];
}

// ============================================
// Azure Event Handler
// ============================================

/**
 * @brief Handle Azure Realtime API events
 *
 * @param json_str JSON event string
 * @param event_type Parsed event type
 */
static void handle_azure_event(const char *json_str, const char *event_type)
{
    cJSON *root = cJSON_Parse(json_str);
    if (!root) {
        ESP_LOGW(TAG, "Failed to parse JSON");
        return;
    }

    // Session events
    if (strcmp(event_type, "session.created") == 0) {
        ESP_LOGI(TAG, "✅ Session created");
        s_state = AZURE_STATE_READY;

        // Parse session ID
        cJSON *session = cJSON_GetObjectItem(root, "session");
        if (session) {
            cJSON *id = cJSON_GetObjectItem(session, "id");
            if (id && cJSON_IsString(id)) {
                strncpy(s_session_id, id->valuestring, sizeof(s_session_id) - 1);
                ESP_LOGI(TAG, "Session ID: %s", s_session_id);
            }
        }

        if (s_config.callback) {
            azure_event_t event = {.type = AZURE_MSG_TYPE_SESSION_CREATED};
            s_config.callback(&event, s_config.user_data);
        }
    }
    else if (strcmp(event_type, "session.updated") == 0) {
        ESP_LOGI(TAG, "✅ Session updated");
        if (s_config.callback) {
            azure_event_t event = {.type = AZURE_MSG_TYPE_SESSION_UPDATED};
            s_config.callback(&event, s_config.user_data);
        }
    }
    // Input audio buffer events
    else if (strcmp(event_type, "input_audio_buffer.speech_started") == 0) {
        ESP_LOGI(TAG, "🎤 Server VAD: Speech started");
        if (s_config.callback) {
            azure_event_t event = {.type = AZURE_MSG_TYPE_INPUT_AUDIO_BUFFER_SPEECH_STARTED};
            s_config.callback(&event, s_config.user_data);
        }
    }
    else if (strcmp(event_type, "input_audio_buffer.speech_stopped") == 0) {
        ESP_LOGI(TAG, "🎤 Server VAD: Speech stopped");
        if (s_config.callback) {
            azure_event_t event = {.type = AZURE_MSG_TYPE_INPUT_AUDIO_BUFFER_SPEECH_STOPPED};
            s_config.callback(&event, s_config.user_data);
        }
    }
    else if (strcmp(event_type, "input_audio_buffer.committed") == 0) {
        ESP_LOGI(TAG, "✅ Audio buffer committed");
        if (s_config.callback) {
            azure_event_t event = {.type = AZURE_MSG_TYPE_INPUT_AUDIO_BUFFER_COMMITTED};
            s_config.callback(&event, s_config.user_data);
        }
    }
    // Response events
    else if (strcmp(event_type, "response.created") == 0) {
        ESP_LOGI(TAG, "🤖 Response created - AI responding");
        s_state = AZURE_STATE_STREAMING;

        if (s_config.callback) {
            azure_event_t event = {.type = AZURE_MSG_TYPE_RESPONSE_CREATED};
            s_config.callback(&event, s_config.user_data);
        }
    }
    else if (strcmp(event_type, "response.audio_transcript.delta") == 0) {
        // Parse transcript text
        cJSON *delta = cJSON_GetObjectItem(root, "delta");
        if (delta && cJSON_IsString(delta)) {
            ESP_LOGI(TAG, "📝 Transcript: %s", delta->valuestring);
            if (s_config.callback) {
                azure_event_t event = {
                    .type = AZURE_MSG_TYPE_RESPONSE_AUDIO_TRANSCRIPT_DELTA,
                    .text = delta->valuestring
                };
                s_config.callback(&event, s_config.user_data);
            }
        }
    }
    else if (strcmp(event_type, "response.audio.delta") == 0) {
        // Parse audio delta
        uint8_t ulaw_buffer[2048];
        size_t ulaw_size = sizeof(ulaw_buffer);

        if (azure_protocol_parse_audio_delta(json_str, ulaw_buffer, &ulaw_size)) {
            // Decode G.711 μ-law → PCM16
            static int16_t pcm_buffer[2048];
            for (size_t i = 0; i < ulaw_size; i++) {
                pcm_buffer[i] = ulaw_to_linear(ulaw_buffer[i]);
            }

            ESP_LOGD(TAG, "🔊 Audio delta: %d bytes μ-law → %d bytes PCM16", ulaw_size, ulaw_size * 2);

            if (s_config.callback) {
                azure_event_t event = {
                    .type = AZURE_MSG_TYPE_RESPONSE_AUDIO_DELTA,
                    .audio_data = (uint8_t*)pcm_buffer,
                    .audio_size = ulaw_size * 2
                };
                s_config.callback(&event, s_config.user_data);
            }
        }
    }
    else if (strcmp(event_type, "response.audio.done") == 0) {
        ESP_LOGI(TAG, "🔊 Audio stream complete");
        if (s_config.callback) {
            azure_event_t event = {.type = AZURE_MSG_TYPE_RESPONSE_AUDIO_DONE};
            s_config.callback(&event, s_config.user_data);
        }
    }
    else if (strcmp(event_type, "response.done") == 0) {
        ESP_LOGI(TAG, "✅ Response complete");
        s_state = AZURE_STATE_READY;

        if (s_config.callback) {
            azure_event_t event = {.type = AZURE_MSG_TYPE_RESPONSE_DONE};
            s_config.callback(&event, s_config.user_data);
        }
    }
    // Error event
    else if (strcmp(event_type, "error") == 0) {
        char error_msg[256] = {0};
        int error_code = 0;

        if (azure_protocol_parse_error(json_str, error_msg, sizeof(error_msg), &error_code)) {
            ESP_LOGE(TAG, "❌ Error: %s (code: %d)", error_msg, error_code);

            if (s_config.callback) {
                azure_event_t event = {
                    .type = AZURE_MSG_TYPE_ERROR,
                    .error_message = error_msg,
                    .error_code = error_code
                };
                s_config.callback(&event, s_config.user_data);
            }
        }
    }
    else {
        ESP_LOGW(TAG, "⚠️ Unknown event: %s", event_type);
    }

    cJSON_Delete(root);
}

// ============================================
// WebSocket Client Validation
// ============================================

/**
 * @brief Check if WebSocket client is in a valid state for operations
 *
 * @return true if client is valid and ready for operations
 */
static inline bool is_ws_client_valid(void)
{
    return (s_ws_client != NULL &&
            (s_state == AZURE_STATE_CONNECTED ||
             s_state == AZURE_STATE_READY ||
             s_state == AZURE_STATE_STREAMING));
}

// ============================================
// Azure WebSocket Task - Audio Batching
// ============================================

/**
 * @brief Azure WebSocket task - handles connection and audio streaming
 *
 * Uses batch sending to reduce WebSocket message frequency and improve throughput.
 * Accumulates AUDIO_BATCH_FRAMES frames before sending, or sends on timeout.
 */
static void azure_realtime_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Azure Realtime task started (batch mode: %d frames, %dms timeout)",
             AUDIO_BATCH_FRAMES, AUDIO_BATCH_TIMEOUT_MS);

    audio_chunk_t chunk;
    static char send_buffer[WS_BUFFER_SIZE];  // Static to avoid 8KB stack usage

    // Batch buffer for accumulating audio frames
    static uint8_t batch_buffer[AZURE_AUDIO_CHUNK_SIZE * AUDIO_BATCH_FRAMES];
    size_t batch_len = 0;
    int batch_frames = 0;
    uint32_t batch_start_tick = 0;

    while (s_task_running) {
        // Handle pending cleanup requested by event callbacks
        if (s_ws_cleanup_needed) {
            azure_realtime_disconnect();
            s_ws_cleanup_needed = false;
        }

        // Handle reconnection if disconnected
        if (s_state == AZURE_STATE_DISCONNECTED) {
            ESP_LOGI(TAG, "Attempting reconnection...");
            azure_realtime_disconnect();  // ensure previous client is fully cleaned
            esp_err_t ret = azure_realtime_connect();
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "Reconnection failed, will retry in %dms", RECONNECT_DELAY_MS);
            }
            vTaskDelay(pdMS_TO_TICKS(RECONNECT_DELAY_MS));
            continue;
        }

        // Send pending session.update after successful connection
        if (s_state == AZURE_STATE_CONNECTED && s_session_update_pending) {
            if (s_ws_client && esp_websocket_client_is_connected(s_ws_client)) {
                char session_buf[2048];
                int len = azure_protocol_build_session_update(session_buf, sizeof(session_buf));
                if (len > 0) {
                    int ret = esp_websocket_client_send_text(s_ws_client, session_buf, len, pdMS_TO_TICKS(1000));
                    if (ret >= 0) {
                        ESP_LOGI(TAG, "📤 Sent session.update");
                        s_session_update_pending = false;
                    } else {
                        ESP_LOGW(TAG, "Failed to send session.update (ret=%d), will retry", ret);
                    }
                } else {
                    ESP_LOGE(TAG, "Failed to build session.update");
                    s_session_update_pending = false;
                }
            } else {
                // Not really connected; mark for cleanup and retry
                s_ws_cleanup_needed = true;
            }
        }

        // Process audio queue if connected and ready
        if (s_state == AZURE_STATE_READY || s_state == AZURE_STATE_STREAMING) {
            // Try to receive audio with short timeout
            if (xQueueReceive(s_audio_queue, &chunk, pdMS_TO_TICKS(20)) == pdTRUE) {
                // Log first chunk to confirm audio flow
                if (batch_frames == 0 && s_send_count < 3) {
                    ESP_LOGI(TAG, "🎤 Audio chunk received (state=%d, queue=%d)",
                             s_state, (int)uxQueueMessagesWaiting(s_audio_queue));
                }
                // Start batch timer on first frame
                if (batch_frames == 0) {
                    batch_start_tick = xTaskGetTickCount();
                }

                // Add to batch buffer
                if (batch_len + chunk.size <= sizeof(batch_buffer)) {
                    memcpy(batch_buffer + batch_len, chunk.data, chunk.size);
                    batch_len += chunk.size;
                    batch_frames++;
                }
            }

            // Check if we should send the batch
            bool should_send = false;
            uint32_t elapsed_ms = (xTaskGetTickCount() - batch_start_tick) * portTICK_PERIOD_MS;

            if (batch_frames >= AUDIO_BATCH_FRAMES) {
                should_send = true;  // Reached batch size
            } else if (batch_frames > 0 && elapsed_ms >= AUDIO_BATCH_TIMEOUT_MS) {
                should_send = true;  // Timeout with pending data
            }

            // Send batch if ready
            if (should_send && batch_len > 0) {
                // Validate client before sending
                if (!is_ws_client_valid()) {
                    ESP_LOGW(TAG, "WebSocket client invalid, dropping audio batch");
                    batch_len = 0;
                    batch_frames = 0;
                    continue;
                }

                // Convert PCM16 to G.711 μ-law (2:1 compression)
                static uint8_t ulaw_buffer[AZURE_AUDIO_CHUNK_SIZE * AUDIO_BATCH_FRAMES];
                size_t ulaw_len = 0;
                int16_t *pcm_samples = (int16_t *)batch_buffer;
                size_t num_samples = batch_len / 2;  // 16-bit samples = bytes / 2

                for (size_t i = 0; i < num_samples; i++) {
                    ulaw_buffer[ulaw_len++] = linear_to_ulaw(pcm_samples[i]);
                }

                // Build WebSocket message with G.711 μ-law data
                int len = azure_protocol_build_audio_append(send_buffer, sizeof(send_buffer),
                                                             ulaw_buffer, ulaw_len);
                if (len > 0) {
                    s_send_count++;
                    ESP_LOGI(TAG, "📤 SEND #%d: %d frames, PCM:%zu → μ-law:%zu → WS:%d bytes (heap: %lu)",
                             s_send_count, batch_frames, batch_len, ulaw_len, len, esp_get_free_heap_size());
                    int ret = esp_websocket_client_send_text(s_ws_client, send_buffer, len, pdMS_TO_TICKS(200));
                    if (ret < 0) {
                        ESP_LOGE(TAG, "❌ WebSocket send failed: %d", ret);
                    }
                    // TLS recovery delay: 70ms between sends to prevent transport errors
                    vTaskDelay(pdMS_TO_TICKS(70));
                }

                // Reset batch
                batch_len = 0;
                batch_frames = 0;
            }
        } else {
            // Not connected - clear any pending batch and wait
            batch_len = 0;
            batch_frames = 0;
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }

    ESP_LOGI(TAG, "Azure Realtime task stopped");
    vTaskDelete(NULL);
}

// ============================================
// WebSocket Event Handler
// ============================================

static void websocket_event_handler(void *handler_args, esp_event_base_t base,
                                      int32_t event_id, void *event_data)
{
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;

    switch (event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
        ESP_LOGI(TAG, "✅ WebSocket Connected to Azure OpenAI Realtime");
        s_state = AZURE_STATE_CONNECTED;
        s_send_count = 0;
        s_recv_count = 0;
        s_ws_cleanup_needed = false;
        s_session_update_pending = true;
        // Track the active client from event data
        if (data && data->client) {
            s_ws_client = data->client;
        }
        break;

    case WEBSOCKET_EVENT_DATA:
        if (data->op_code == 0x01) {  // Text frame
            ESP_LOGI(TAG, "📥 Received WebSocket message (%d bytes)", data->data_len);

            // Null-terminate the data for cJSON parsing
            char *json_str = (char *)data->data_ptr;
            char temp_buf[8192];
            if (data->data_len < sizeof(temp_buf)) {
                memcpy(temp_buf, json_str, data->data_len);
                temp_buf[data->data_len] = '\0';

                // Parse event type
                char event_type[128];
                if (azure_protocol_parse_event_type(temp_buf, event_type, sizeof(event_type))) {
                    ESP_LOGI(TAG, "Event type: %s", event_type);
                    handle_azure_event(temp_buf, event_type);
                } else {
                    ESP_LOGW(TAG, "Failed to parse event type");
                }
            }
        }
        break;

    case WEBSOCKET_EVENT_ERROR:
        ESP_LOGE(TAG, "❌ WebSocket Error");
        // Set state to DISCONNECTED (not ERROR) to allow reconnection
        // The azure_realtime_task will handle reconnection with proper cleanup
        s_state = AZURE_STATE_DISCONNECTED;
        s_ws_cleanup_needed = true;
        s_session_update_pending = false;

        // Clear audio queue to prevent stale data on reconnection
        if (s_audio_queue) {
            audio_chunk_t chunk;
            while (xQueueReceive(s_audio_queue, &chunk, 0) == pdTRUE) {
                // Drain queue
            }
            ESP_LOGI(TAG, "Audio queue drained after error");
        }

        // Note: Avoid destroying client in callback context; task loop will clean & reconnect
        break;

    case WEBSOCKET_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "⚠️ WebSocket Disconnected");
        s_state = AZURE_STATE_DISCONNECTED;
        s_ws_cleanup_needed = true;
        s_session_update_pending = false;
        break;

    default:
        break;
    }
}

// ============================================
// Public API Implementation
// ============================================

esp_err_t azure_realtime_init(void)
{
    ESP_LOGI(TAG, "Initializing Azure Realtime client");

    // Create audio queue
    s_audio_queue = xQueueCreate(AUDIO_QUEUE_SIZE, sizeof(audio_chunk_t));
    if (s_audio_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create audio queue");
        return ESP_ERR_NO_MEM;
    }

    s_state = AZURE_STATE_DISCONNECTED;
    return ESP_OK;
}

esp_err_t azure_realtime_deinit(void)
{
    if (s_ws_client) {
        esp_websocket_client_stop(s_ws_client);
        esp_websocket_client_destroy(s_ws_client);
        s_ws_client = NULL;
    }

    if (s_audio_queue) {
        vQueueDelete(s_audio_queue);
        s_audio_queue = NULL;
    }

    return ESP_OK;
}

esp_err_t azure_realtime_configure(const azure_realtime_config_t *config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(&s_config, config, sizeof(azure_realtime_config_t));
    ESP_LOGI(TAG, "Configured: resource=%s, deployment=%s",
             s_config.resource_name, s_config.deployment_name);

    return ESP_OK;
}

esp_err_t azure_realtime_connect(void)
{
    // Prevent concurrent connection attempts
    if (s_state == AZURE_STATE_CONNECTING || s_state == AZURE_STATE_CONNECTED ||
        s_state == AZURE_STATE_READY || s_state == AZURE_STATE_STREAMING) {
        ESP_LOGW(TAG, "Already connecting or connected (state=%d)", s_state);
        return ESP_OK;
    }

    if (s_config.api_key == NULL || s_config.resource_name == NULL) {
        ESP_LOGE(TAG, "Missing API key or resource name");
        return ESP_ERR_INVALID_STATE;
    }

    // Build WebSocket URL - use endpoint directly
    char url[512];
    if (s_config.endpoint) {
        // Use endpoint directly
        snprintf(url, sizeof(url),
                 "wss://%s/openai/realtime?api-version=%s&deployment=%s",
                 s_config.endpoint,
                 AZURE_REALTIME_API_VERSION,
                 s_config.deployment_name);
    } else if (s_config.resource_name) {
        // Fallback to resource_name (deprecated)
        snprintf(url, sizeof(url),
                 "wss://%s.openai.azure.com/openai/realtime?api-version=%s&deployment=%s",
                 s_config.resource_name,
                 AZURE_REALTIME_API_VERSION,
                 s_config.deployment_name);
    } else {
        ESP_LOGE(TAG, "Missing endpoint or resource_name");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Connecting to: %s", url);

    // IMPORTANT: Destroy any existing client before creating a new one
    if (s_ws_client != NULL) {
        ESP_LOGW(TAG, "Destroying existing WebSocket client before reconnection");
        esp_websocket_client_stop(s_ws_client);
        esp_websocket_client_destroy(s_ws_client);
        s_ws_client = NULL;
    }

    // Mark as connecting after cleanup
    s_state = AZURE_STATE_CONNECTING;

    // WebSocket config with complete TLS/SSL support for Azure
    esp_websocket_client_config_t ws_cfg = {
        .uri = url,
        .task_stack = 8192,
        .buffer_size = 8192,  // Azure sends larger frames
        .crt_bundle_attach = esp_crt_bundle_attach,  // Attach ESP-IDF CA bundle

        // CRITICAL: Disable auto-reconnect to prevent crash during SSL failures
        // We handle reconnection manually in azure_realtime_task()
        .disable_auto_reconnect = true,

        // CRITICAL: Azure hostname verification
        .skip_cert_common_name_check = true,  // Azure uses wildcard certs

        // Connection stability for Azure
        .keep_alive_enable = true,
        .keep_alive_idle = 10,
        .keep_alive_interval = 10,
        .keep_alive_count = 5,

        // Azure cloud needs longer timeouts
        .network_timeout_ms = 30000,
        .pingpong_timeout_sec = 60,
    };

    s_ws_client = esp_websocket_client_init(&ws_cfg);
    if (s_ws_client == NULL) {
        ESP_LOGE(TAG, "Failed to init WebSocket client");
        s_state = AZURE_STATE_DISCONNECTED;  // Reset state on failure
        return ESP_FAIL;
    }

    // Set API key header
    esp_websocket_client_append_header(s_ws_client, "api-key", s_config.api_key);

    // Register event handler
    esp_websocket_register_events(s_ws_client, WEBSOCKET_EVENT_ANY,
                                   websocket_event_handler, NULL);

    // Start connection (state already set to CONNECTING at function entry)
    esp_err_t ret = esp_websocket_client_start(s_ws_client);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start WebSocket client: %s", esp_err_to_name(ret));
        s_state = AZURE_STATE_DISCONNECTED;  // Reset state on failure
    }
    return ret;
}

esp_err_t azure_realtime_disconnect(void)
{
    if (s_ws_client) {
        esp_websocket_client_stop(s_ws_client);
        esp_websocket_client_destroy(s_ws_client);
        s_ws_client = NULL;
    }
    s_state = AZURE_STATE_DISCONNECTED;
    return ESP_OK;
}

bool azure_realtime_is_connected(void)
{
    return (s_state == AZURE_STATE_READY || s_state == AZURE_STATE_STREAMING);
}

azure_state_t azure_realtime_get_state(void)
{
    return s_state;
}

esp_err_t azure_realtime_start_session(void)
{
    // Session is automatically started on connection
    return ESP_OK;
}

esp_err_t azure_realtime_end_session(void)
{
    return azure_realtime_disconnect();
}

esp_err_t azure_realtime_send_audio(const uint8_t *audio_data, size_t size)
{
    if (audio_data == NULL || s_audio_queue == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!azure_realtime_is_connected()) {
        ESP_LOGW(TAG, "Not connected, dropping audio");
        return ESP_ERR_INVALID_STATE;
    }

    // Queue audio data in chunks
    size_t offset = 0;
    int chunks_queued = 0;
    while (offset < size) {
        audio_chunk_t chunk;
        chunk.size = (size - offset > AZURE_AUDIO_CHUNK_SIZE) ?
                     AZURE_AUDIO_CHUNK_SIZE : (size - offset);
        memcpy(chunk.data, audio_data + offset, chunk.size);

        if (xQueueSend(s_audio_queue, &chunk, pdMS_TO_TICKS(10)) != pdTRUE) {
            ESP_LOGW(TAG, "Audio queue full, dropping chunk");
        } else {
            chunks_queued++;
        }

        offset += chunk.size;
    }

    // Log periodically
    static uint32_t total_queued = 0;
    total_queued += chunks_queued;
    if (total_queued % 50 == 0) {
        ESP_LOGI(TAG, "🎙️ Audio queued: total=%lu, this call=%d chunks, %u bytes",
                 total_queued, chunks_queued, (unsigned)size);
    }

    return ESP_OK;
}

esp_err_t azure_realtime_commit_audio(void)
{
    if (!is_ws_client_valid()) {
        ESP_LOGW(TAG, "Cannot commit audio: WebSocket not connected");
        return ESP_ERR_INVALID_STATE;
    }

    char buffer[128];
    int len = azure_protocol_build_audio_commit(buffer, sizeof(buffer));
    if (len > 0) {
        ESP_LOGI(TAG, "📤 Sending input_audio_buffer.commit");
        return esp_websocket_client_send_text(s_ws_client, buffer, len, portMAX_DELAY);
    }

    return ESP_FAIL;
}

esp_err_t azure_realtime_create_response(void)
{
    if (!is_ws_client_valid()) {
        ESP_LOGW(TAG, "Cannot create response: WebSocket not connected");
        return ESP_ERR_INVALID_STATE;
    }

    char buffer[512];
    int len = azure_protocol_build_response_create(buffer, sizeof(buffer));
    if (len > 0) {
        ESP_LOGI(TAG, "📤 Sending response.create");
        return esp_websocket_client_send_text(s_ws_client, buffer, len, portMAX_DELAY);
    }

    return ESP_FAIL;
}

esp_err_t azure_realtime_cancel_response(void)
{
    if (!is_ws_client_valid()) {
        ESP_LOGW(TAG, "Cannot cancel response: WebSocket not connected");
        return ESP_ERR_INVALID_STATE;
    }

    char buffer[128];
    int len = azure_protocol_build_response_cancel(buffer, sizeof(buffer));
    if (len > 0) {
        ESP_LOGI(TAG, "📤 Sending response.cancel");
        return esp_websocket_client_send_text(s_ws_client, buffer, len, portMAX_DELAY);
    }

    return ESP_FAIL;
}

esp_err_t azure_realtime_register_callback(azure_event_callback_t callback, void *user_data)
{
    s_config.callback = callback;
    s_config.user_data = user_data;
    return ESP_OK;
}

const char *azure_realtime_get_session_id(void)
{
    return s_session_id[0] ? s_session_id : NULL;
}

esp_err_t azure_realtime_start_task(void)
{
    if (s_task_running) {
        ESP_LOGW(TAG, "Task already running");
        return ESP_OK;
    }

    // Create audio queue
    if (s_audio_queue == NULL) {
        s_audio_queue = xQueueCreate(AUDIO_QUEUE_SIZE, sizeof(audio_chunk_t));
        if (s_audio_queue == NULL) {
            ESP_LOGE(TAG, "Failed to create audio queue");
            return ESP_FAIL;
        }
    }

    // Start task
    s_task_running = true;
    BaseType_t ret = xTaskCreate(azure_realtime_task, "azure_rt_task",
                                  8192, NULL, 5, &s_task_handle);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create task");
        s_task_running = false;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Azure Realtime task started");
    return ESP_OK;
}

esp_err_t azure_realtime_stop_task(void)
{
    if (!s_task_running) {
        return ESP_OK;
    }

    s_task_running = false;

    // Wait for task to finish
    if (s_task_handle) {
        vTaskDelay(pdMS_TO_TICKS(100));  // Give task time to exit
        s_task_handle = NULL;
    }

    // Clean up queue
    if (s_audio_queue) {
        vQueueDelete(s_audio_queue);
        s_audio_queue = NULL;
    }

    // Ensure websocket client is stopped/destroyed
    azure_realtime_disconnect();

    ESP_LOGI(TAG, "Azure Realtime task stopped");
    return ESP_OK;
}

// ============================================
// Debug Helper Functions
// ============================================

const char *azure_realtime_msg_type_to_string(azure_msg_type_t type)
{
    switch (type) {
    case AZURE_MSG_TYPE_SESSION_CREATED:            return "SESSION_CREATED";
    case AZURE_MSG_TYPE_SESSION_UPDATED:            return "SESSION_UPDATED";
    case AZURE_MSG_TYPE_INPUT_AUDIO_BUFFER_SPEECH_STARTED:  return "SPEECH_STARTED";
    case AZURE_MSG_TYPE_INPUT_AUDIO_BUFFER_SPEECH_STOPPED:  return "SPEECH_STOPPED";
    case AZURE_MSG_TYPE_INPUT_AUDIO_BUFFER_COMMITTED:       return "AUDIO_COMMITTED";
    case AZURE_MSG_TYPE_RESPONSE_CREATED:           return "RESPONSE_CREATED";
    case AZURE_MSG_TYPE_RESPONSE_AUDIO_TRANSCRIPT_DELTA:    return "TRANSCRIPT_DELTA";
    case AZURE_MSG_TYPE_RESPONSE_AUDIO_DELTA:       return "AUDIO_DELTA";
    case AZURE_MSG_TYPE_RESPONSE_AUDIO_DONE:        return "AUDIO_DONE";
    case AZURE_MSG_TYPE_RESPONSE_DONE:              return "RESPONSE_DONE";
    case AZURE_MSG_TYPE_ERROR:                      return "ERROR";
    default:                                         return "UNKNOWN";
    }
}

const char *azure_realtime_state_to_string(azure_state_t state)
{
    switch (state) {
    case AZURE_STATE_DISCONNECTED:  return "DISCONNECTED";
    case AZURE_STATE_CONNECTING:    return "CONNECTING";
    case AZURE_STATE_CONNECTED:     return "CONNECTED";
    case AZURE_STATE_READY:         return "READY";
    case AZURE_STATE_STREAMING:     return "STREAMING";
    case AZURE_STATE_ERROR:         return "ERROR";
    default:                        return "UNKNOWN";
    }
}
