/**
 * @file coze_ws.c
 * @brief Coze WebSocket client implementation (Audio Speech API)
 */

#include "coze_ws.h"
#include "coze_protocol.h"
#include "app_core.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_websocket_client.h"
#include "esp_event.h"
#include "esp_tls.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"

static const char *TAG = "COZE_WS";

// ============================================
// G.711 μ-law Encoding
// ============================================

// G.711 μ-law compression table (bias = 0x84, compressed to 8-bit)
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
// Configuration
// ============================================

#define WS_BUFFER_SIZE          8192    // Increased from 4096 for Base64-encoded 60ms audio frames (~5200 bytes needed)
#define WS_TASK_STACK_SIZE      12288  // TLS + JSON + WebSocket needs ~12KB (internal RAM only!)
#define AUDIO_QUEUE_SIZE        20     // 20 items * 60ms = 1.2s buffer (reduced from 50 due to larger 1920-byte frames)
#define AUDIO_BATCH_FRAMES      2      // Send 2 frames (~120ms) at a time (reduced from 4 since frames are now 60ms)
#define AUDIO_BATCH_TIMEOUT_MS  100    // Or timeout after 100ms
#define RECONNECT_DELAY_MS      5000

// ============================================
// Private Variables
// ============================================

static bool s_initialized = false;
static coze_state_t s_state = COZE_STATE_DISCONNECTED;
static coze_ws_config_t s_config;

// Static buffer for error messages (prevents crash from invalid pointers)
static char s_error_msg_buffer[COZE_MAX_ERROR_MSG_LEN] = {0};

// WebSocket client
static esp_websocket_client_handle_t s_ws_client = NULL;

// Session info
static char s_session_id[COZE_MAX_SESSION_ID_LEN] = {0};
static char s_conversation_id[COZE_MAX_CONVERSATION_ID_LEN] = {0};

// Callback
static coze_event_callback_t s_event_callback = NULL;
static void *s_callback_user_data = NULL;

// Task
static TaskHandle_t s_ws_task = NULL;
static volatile bool s_task_running = false;

// Audio send queue
static QueueHandle_t s_audio_queue = NULL;

// Mutex
static SemaphoreHandle_t s_mutex = NULL;

// Debug counters
static int s_send_count = 0;
static int s_recv_count = 0;

// Audio chunk structure
typedef struct {
    uint8_t data[COZE_AUDIO_CHUNK_SIZE];
    size_t size;
} audio_chunk_t;

// ============================================
// Private Functions - Event Handling
// ============================================

/**
 * @brief Parse and dispatch received message
 */
static void handle_received_message(const char *data, int len)
{
    if (data == NULL || len <= 0) return;

    // DEBUG: Log raw JSON for debugging (use ESP_LOGE to ensure visibility)
    ESP_LOGE(TAG, "📥 RECV RAW (%d bytes): %.*s", len, len > 500 ? 500 : len, data);

    // Parse event type
    char event_type[64] = {0};
    if (!coze_protocol_parse_event_type(data, event_type, sizeof(event_type))) {
        ESP_LOGE(TAG, "❌ Failed to parse event type from: %.*s", len > 200 ? 200 : len, data);
        return;
    }

    ESP_LOGE(TAG, "📥 RECV EVENT: %s", event_type);

    coze_event_t event = {0};

    // Handle different event types (Coze Audio Speech WebSocket API)
    if (strcmp(event_type, COZE_EVENT_SPEECH_CREATED) == 0) {
        event.type = COZE_MSG_TYPE_SPEECH_CREATED;
        coze_protocol_parse_chat_id(data, s_session_id, sizeof(s_session_id));
        event.session_id = s_session_id;
        s_state = COZE_STATE_READY;
        ESP_LOGI(TAG, "✅ Speech session created: id=%s", s_session_id);
        // Pure audio mode: Wait for user to trigger voice input via button

    } else if (strcmp(event_type, COZE_EVENT_SESSION_UPDATED) == 0) {
        event.type = COZE_MSG_TYPE_SESSION_UPDATED;
        s_state = COZE_STATE_READY;
        ESP_LOGI(TAG, "✅ Session updated");

    } else if (strcmp(event_type, COZE_EVENT_INPUT_AUDIO_BUFFER_SPEECH_STARTED) == 0) {
        event.type = COZE_MSG_TYPE_INPUT_AUDIO_BUFFER_SPEECH_STARTED;
        s_state = COZE_STATE_STREAMING;
        ESP_LOGI(TAG, "🎤 Speech started (VAD detected)");

    } else if (strcmp(event_type, COZE_EVENT_INPUT_AUDIO_BUFFER_SPEECH_STOPPED) == 0) {
        event.type = COZE_MSG_TYPE_INPUT_AUDIO_BUFFER_SPEECH_STOPPED;
        ESP_LOGI(TAG, "🎤 Speech stopped (VAD detected)");

    // Coze protocol uses conversation.* events, not response.* events
    // REMOVED: COZE_EVENT_RESPONSE_CREATED - not used by Coze
    // REMOVED: COZE_EVENT_RESPONSE_AUDIO_TRANSCRIPT_DELTA - not used by Coze

    } else if (strcmp(event_type, COZE_EVENT_CONVERSATION_AUDIO_DELTA) == 0) {
        event.type = COZE_MSG_TYPE_RESPONSE_AUDIO_DELTA;
        static uint8_t ulaw_buffer[2048];  // G.711 μ-law data from server
        static uint8_t pcm_buffer[4096];   // PCM16 data for playback (2x size)
        size_t ulaw_size = 0;

        // Parse base64-encoded G.711 μ-law audio from server (Coze protocol)
        if (coze_protocol_parse_audio_delta(data, ulaw_buffer, &ulaw_size, sizeof(ulaw_buffer))) {
            // Convert G.711 μ-law to PCM16 for playback
            int16_t *pcm_samples = (int16_t *)pcm_buffer;
            for (size_t i = 0; i < ulaw_size; i++) {
                pcm_samples[i] = ulaw_to_linear(ulaw_buffer[i]);
            }

            event.audio_data = pcm_buffer;
            event.audio_size = ulaw_size * 2;  // PCM16 is 2 bytes per sample
            ESP_LOGI(TAG, "🔊 Conversation audio delta: μ-law:%zu → PCM16:%zu bytes", ulaw_size, event.audio_size);
        }

    } else if (strcmp(event_type, COZE_EVENT_CONVERSATION_CHAT_COMPLETED) == 0) {
        event.type = COZE_MSG_TYPE_RESPONSE_DONE;
        s_state = COZE_STATE_READY;
        ESP_LOGI(TAG, "✅ Conversation chat completed");

    } else if (strcmp(event_type, COZE_EVENT_CONVERSATION_CHAT_CANCELED) == 0) {
        event.type = COZE_MSG_TYPE_RESPONSE_DONE;
        s_state = COZE_STATE_READY;
        ESP_LOGI(TAG, "⚠️  Conversation chat canceled");

    } else if (strcmp(event_type, COZE_EVENT_ERROR) == 0) {
        event.type = COZE_MSG_TYPE_ERROR;
        char temp_error_msg[COZE_MAX_ERROR_MSG_LEN];
        int error_code = 0;
        if (coze_protocol_parse_error(data, temp_error_msg, sizeof(temp_error_msg), &error_code)) {
            // Copy to file-scope static buffer to prevent pointer invalidation
            strncpy(s_error_msg_buffer, temp_error_msg, sizeof(s_error_msg_buffer) - 1);
            s_error_msg_buffer[sizeof(s_error_msg_buffer) - 1] = '\0';
            event.error_message = s_error_msg_buffer;
            event.error_code = error_code;
            ESP_LOGE(TAG, "❌ Error: %s (code=%d)", s_error_msg_buffer, error_code);
        }
        s_state = COZE_STATE_ERROR;

    } else {
        ESP_LOGW(TAG, "⚠️ Unhandled event: %s", event_type);
        return;
    }

    // Notify callback with comprehensive validation
    ESP_LOGI(TAG, "🔍 About to invoke callback: ptr=%p, event_type=%d",
             (void*)s_event_callback, event.type);

    // Validate function pointer is in valid memory range
    // Valid ESP32 code ranges: 0x40000000-0x50000000
    if (s_event_callback) {
        if ((uintptr_t)s_event_callback < 0x40000000 ||
            (uintptr_t)s_event_callback > 0x50000000) {
            ESP_LOGE(TAG, "❌ INVALID callback pointer: %p - OUT OF VALID CODE RANGE",
                     (void*)s_event_callback);
            ESP_LOGE(TAG, "❌ System may be corrupted! Skipping callback to prevent crash.");
            return;
        }

        ESP_LOGI(TAG, "✅ Calling callback...");
        s_event_callback(&event, s_callback_user_data);
        ESP_LOGI(TAG, "✅ Callback returned successfully");
    } else {
        ESP_LOGW(TAG, "⚠️  Callback is NULL, skipping");
    }
}

/**
 * @brief WebSocket event handler
 */
static void websocket_event_handler(void *handler_args, esp_event_base_t base,
                                     int32_t event_id, void *event_data)
{
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;

    switch (event_id) {
        case WEBSOCKET_EVENT_CONNECTED:
            ESP_LOGE(TAG, "🟢 WS_EVENT: CONNECTED - resetting counters");
            s_send_count = 0;
            s_recv_count = 0;
            s_state = COZE_STATE_CONNECTED;

            // Send session configuration
            coze_ws_start_session();

            // Solution 2: Reconnect recovery - resend complete if in PROCESSING state
            // If we disconnected while waiting for AI response, automatically resend complete
            if (app_core_get_state() == APP_STATE_PROCESSING) {
                ESP_LOGW(TAG, "🔄 Reconnected in PROCESSING state - resending input_audio_buffer.complete");
                vTaskDelay(pdMS_TO_TICKS(500));  // Wait for session to be ready
                coze_ws_commit_audio();
            }
            break;

        case WEBSOCKET_EVENT_DISCONNECTED:
            ESP_LOGE(TAG, "🔴 WS_EVENT: DISCONNECTED (sent=%d, recv=%d)", s_send_count, s_recv_count);
            s_state = COZE_STATE_DISCONNECTED;
            break;

        case WEBSOCKET_EVENT_DATA:
            s_recv_count++;
            ESP_LOGE(TAG, "📥 WS_EVENT: DATA #%d (opcode=0x%02X, len=%d)",
                     s_recv_count, data->op_code, data->data_len);
            if (data->op_code == 0x01) {  // Text frame
                handle_received_message((char *)data->data_ptr, data->data_len);
            } else if (data->op_code == 0x02) {  // Binary frame
                ESP_LOGE(TAG, "📥 WS_EVENT: Binary frame %d bytes", data->data_len);
            } else if (data->op_code == 0x09) {  // Ping
                ESP_LOGE(TAG, "📥 WS_EVENT: PING received");
            } else if (data->op_code == 0x0A) {  // Pong
                ESP_LOGE(TAG, "📥 WS_EVENT: PONG received");
            } else {
                ESP_LOGE(TAG, "📥 WS_EVENT: Unknown opcode 0x%02X", data->op_code);
            }
            break;

        case WEBSOCKET_EVENT_ERROR:
            ESP_LOGE(TAG, "🔴 WS_EVENT: ERROR");
            s_state = COZE_STATE_ERROR;
            break;

        case WEBSOCKET_EVENT_CLOSED:
            ESP_LOGE(TAG, "🔴 WS_EVENT: CLOSED (sent=%d, recv=%d)", s_send_count, s_recv_count);
            s_state = COZE_STATE_DISCONNECTED;
            break;

        default:
            ESP_LOGE(TAG, "⚠️ WS_EVENT: UNKNOWN event_id=%ld", event_id);
            break;
    }
}

/**
 * @brief WebSocket task - handles connection and audio streaming
 *
 * Uses batch sending to reduce WebSocket message frequency and improve throughput.
 * Accumulates AUDIO_BATCH_FRAMES frames before sending, or sends on timeout.
 */
static void coze_ws_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Coze WebSocket task started (batch mode: %d frames, %dms timeout)",
             AUDIO_BATCH_FRAMES, AUDIO_BATCH_TIMEOUT_MS);

    audio_chunk_t chunk;
    static char send_buffer[WS_BUFFER_SIZE];  // Static to avoid 8KB stack usage

    // Batch buffer for accumulating audio frames
    static uint8_t batch_buffer[COZE_AUDIO_CHUNK_SIZE * AUDIO_BATCH_FRAMES];
    size_t batch_len = 0;
    int batch_frames = 0;
    uint32_t batch_start_tick = 0;

    while (s_task_running) {
        // Handle reconnection if needed
        if (s_state == COZE_STATE_DISCONNECTED && s_ws_client != NULL) {
            ESP_LOGI(TAG, "Attempting reconnection...");
            coze_ws_connect();
            vTaskDelay(pdMS_TO_TICKS(RECONNECT_DELAY_MS));
            continue;
        }

        // Process audio queue if connected and ready
        if (s_state == COZE_STATE_READY || s_state == COZE_STATE_STREAMING) {
            // Try to receive audio with short timeout
            if (xQueueReceive(s_audio_queue, &chunk, pdMS_TO_TICKS(20)) == pdTRUE) {
                // Log first chunk to confirm audio flow
                if (batch_frames == 0 && s_send_count < 3) {
                    ESP_LOGE(TAG, "🎤 Audio chunk received (state=%d, queue=%d)",
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
                // Convert PCM16 to G.711 μ-law (2:1 compression)
                // 8kHz × 60ms × 2 bytes = 960 bytes per frame, × 2 frames = 1920 bytes max
                static uint8_t ulaw_buffer[1920];  // Max μ-law buffer (960 bytes per frame × 2 frames)
                size_t ulaw_len = 0;
                int16_t *pcm_samples = (int16_t *)batch_buffer;
                size_t num_samples = batch_len / 2;  // 16-bit samples = bytes / 2

                for (size_t i = 0; i < num_samples; i++) {
                    ulaw_buffer[ulaw_len++] = linear_to_ulaw(pcm_samples[i]);
                }

                // Build WebSocket message with G.711 μ-law data
                int len = coze_protocol_build_audio_append(send_buffer, sizeof(send_buffer),
                                                           ulaw_buffer, ulaw_len);
                if (len > 0) {
                    s_send_count++;
                    ESP_LOGI(TAG, "📤 SEND #%d: %d frames, PCM:%zu → μ-law:%zu → WS:%d bytes (heap: %lu)",
                             s_send_count, batch_frames, batch_len, ulaw_len, len, esp_get_free_heap_size());
                    int ret = esp_websocket_client_send_bin(s_ws_client, send_buffer, len, pdMS_TO_TICKS(200));
                    if (ret < 0) {
                        ESP_LOGE(TAG, "❌ WebSocket send failed: %d", ret);
                    }
                    // Solution 1: Increased delay from 30ms to 70ms to achieve ~100ms interval
                    // This reduces transmission rate to prevent transport errors
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

    ESP_LOGI(TAG, "Coze WebSocket task stopped");
    vTaskDelete(NULL);
}

// ============================================
// Public Functions
// ============================================

esp_err_t coze_ws_init(void)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "Coze WS already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing Coze WebSocket client...");

    // Use default config
    s_config = (coze_ws_config_t)COZE_WS_DEFAULT_CONFIG();

    // Create mutex
    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }

    // Create audio queue
    s_audio_queue = xQueueCreate(AUDIO_QUEUE_SIZE, sizeof(audio_chunk_t));
    if (s_audio_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create audio queue");
        return ESP_ERR_NO_MEM;
    }

    // Build WebSocket URI (must be static - ws client stores pointer)
    static char ws_uri[256];
    snprintf(ws_uri, sizeof(ws_uri), "%s%s", COZE_WS_HOST, COZE_WS_PATH);

    // Build Authorization header (REQUIRED for Coze API)
    static char auth_header[256];
    snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s\r\n", s_config.api_token);

    ESP_LOGI(TAG, "WebSocket URI: %s", ws_uri);
    ESP_LOGI(TAG, "Bot ID: %s", s_config.bot_id);

    // Debug: Check memory before TLS connection
    size_t free_heap = esp_get_free_heap_size();
    size_t free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t largest_internal = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    ESP_LOGW(TAG, "Memory before TLS: total=%u, internal=%u, largest_internal_block=%u",
             (unsigned)free_heap, (unsigned)free_internal, (unsigned)largest_internal);

    // Configure WebSocket client
    esp_websocket_client_config_t ws_cfg = {
        .uri = ws_uri,
        .buffer_size = WS_BUFFER_SIZE,
        .crt_bundle_attach = esp_crt_bundle_attach,  // Use ESP-IDF default CA bundle
        .transport = WEBSOCKET_TRANSPORT_OVER_SSL,
        .headers = auth_header,
    };

    // Create WebSocket client
    s_ws_client = esp_websocket_client_init(&ws_cfg);
    if (s_ws_client == NULL) {
        ESP_LOGE(TAG, "Failed to create WebSocket client");
        return ESP_FAIL;
    }

    // Register event handler
    esp_websocket_register_events(s_ws_client, WEBSOCKET_EVENT_ANY,
                                   websocket_event_handler, NULL);

    s_initialized = true;
    ESP_LOGI(TAG, "Coze WebSocket client initialized");

    return ESP_OK;
}

esp_err_t coze_ws_deinit(void)
{
    if (!s_initialized) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Deinitializing Coze WebSocket client...");

    coze_ws_stop_task();
    coze_ws_disconnect();

    if (s_ws_client) {
        esp_websocket_client_destroy(s_ws_client);
        s_ws_client = NULL;
    }

    if (s_audio_queue) {
        vQueueDelete(s_audio_queue);
        s_audio_queue = NULL;
    }

    if (s_mutex) {
        vSemaphoreDelete(s_mutex);
        s_mutex = NULL;
    }

    s_initialized = false;
    ESP_LOGI(TAG, "Coze WebSocket client deinitialized");

    return ESP_OK;
}

esp_err_t coze_ws_configure(const coze_ws_config_t *config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_config = *config;
    xSemaphoreGive(s_mutex);

    return ESP_OK;
}

esp_err_t coze_ws_connect(void)
{
    if (!s_initialized || s_ws_client == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (s_state == COZE_STATE_CONNECTED || s_state == COZE_STATE_READY) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Connecting to Coze server...");
    s_state = COZE_STATE_CONNECTING;

    esp_err_t ret = esp_websocket_client_start(s_ws_client);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start WebSocket client: %s", esp_err_to_name(ret));
        s_state = COZE_STATE_ERROR;
        return ret;
    }

    return ESP_OK;
}

esp_err_t coze_ws_disconnect(void)
{
    if (!s_initialized || s_ws_client == NULL) {
        return ESP_OK;
    }

    if (s_state == COZE_STATE_DISCONNECTED) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Disconnecting from Coze server...");

    esp_websocket_client_stop(s_ws_client);
    s_state = COZE_STATE_DISCONNECTED;

    return ESP_OK;
}

bool coze_ws_is_connected(void)
{
    return s_state == COZE_STATE_READY || s_state == COZE_STATE_STREAMING;
}

coze_state_t coze_ws_get_state(void)
{
    return s_state;
}

esp_err_t coze_ws_start_session(void)
{
    if (!coze_ws_is_connected() && s_state != COZE_STATE_CONNECTED) {
        return ESP_ERR_INVALID_STATE;
    }

    char buffer[1024];
    // Build session.update message for Coze Audio Speech WebSocket API
    // NOTE: Audio Speech API uses "session.update" (NOT "chat.update" which is Chat API)
    int len = coze_protocol_build_chat_update(buffer, sizeof(buffer),
                                               s_config.bot_id, COZE_USER_ID, NULL);
    if (len <= 0) {
        ESP_LOGE(TAG, "Failed to build session.update message");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Starting audio session with bot: %s, user: %s", s_config.bot_id, COZE_USER_ID);
    s_send_count++;
    ESP_LOGE(TAG, "🔴 SEND #%d [SESSION]: session.update (%d bytes)", s_send_count, len);
    ESP_LOGE(TAG, "🔴 SEND #%d [SESSION]: %s", s_send_count, buffer);
    s_state = COZE_STATE_AUTHENTICATING;

    esp_err_t ret = esp_websocket_client_send_bin(s_ws_client, buffer, len, pdMS_TO_TICKS(1000));
    ESP_LOGE(TAG, "🔴 SEND #%d [SESSION]: DONE (ret=%d)", s_send_count, ret);
    return ret;
}

esp_err_t coze_ws_end_session(void)
{
    // Clear session state
    memset(s_session_id, 0, sizeof(s_session_id));
    memset(s_conversation_id, 0, sizeof(s_conversation_id));

    return ESP_OK;
}

static uint32_t s_audio_queued_count = 0;

esp_err_t coze_ws_send_audio(const uint8_t *audio_data, size_t size)
{
    if (!s_initialized || audio_data == NULL) {
        ESP_LOGE(TAG, "❌ send_audio: invalid arg (init=%d, data=%p)", s_initialized, audio_data);
        return ESP_ERR_INVALID_ARG;
    }

    if (!coze_ws_is_connected()) {
        ESP_LOGE(TAG, "❌ send_audio: not connected (state=%d)", s_state);
        return ESP_ERR_INVALID_STATE;
    }

    // Queue audio data in chunks
    size_t offset = 0;
    int chunks_queued = 0;
    while (offset < size) {
        audio_chunk_t chunk;
        chunk.size = (size - offset > COZE_AUDIO_CHUNK_SIZE) ?
                     COZE_AUDIO_CHUNK_SIZE : (size - offset);
        memcpy(chunk.data, audio_data + offset, chunk.size);

        if (xQueueSend(s_audio_queue, &chunk, pdMS_TO_TICKS(10)) != pdTRUE) {
            ESP_LOGW(TAG, "Audio queue full, dropping chunk");
        } else {
            chunks_queued++;
            s_audio_queued_count++;
        }

        offset += chunk.size;
    }

    // Log every 50 chunks
    if (s_audio_queued_count % 50 == 0) {
        ESP_LOGI(TAG, "🎙️ Audio queued: total=%lu, this call=%d chunks, %u bytes",
                 s_audio_queued_count, chunks_queued, (unsigned)size);
    }

    return ESP_OK;
}

esp_err_t coze_ws_send_text(const char *text)
{
    // Pure audio mode: Text messages are NOT supported by Audio Speech WebSocket API
    // The API only accepts: session.update, input_audio_buffer.append/commit, response.create/cancel
    // Use voice input instead - press Boot button to start recording
    ESP_LOGE(TAG, "🔴 BLOCKED: coze_ws_send_text('%s') - NOT SUPPORTED!", text ? text : "NULL");
    ESP_LOGE(TAG, "🔴 BLOCKED: Caller tried to send text message - THIS WOULD CAUSE 4000 ERROR!");
    (void)text;  // Suppress unused parameter warning
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t coze_ws_commit_audio(void)
{
    if (!coze_ws_is_connected()) {
        ESP_LOGE(TAG, "❌ complete_audio: not connected (state=%d)", s_state);
        return ESP_ERR_INVALID_STATE;
    }

    char buffer[256];
    int len = coze_protocol_build_audio_complete(buffer, sizeof(buffer));
    if (len <= 0) {
        return ESP_FAIL;
    }

    // Check WebSocket connection status
    bool ws_connected = esp_websocket_client_is_connected(s_ws_client);
    ESP_LOGE(TAG, "🔴 COMPLETE: ws_connected=%d, state=%d, sent=%d, recv=%d",
             ws_connected, s_state, s_send_count, s_recv_count);

    s_send_count++;
    ESP_LOGE(TAG, "🔴 SEND #%d [COMPLETE]: input_audio_buffer.complete (%d bytes) - AI will auto-respond", s_send_count, len);
    ESP_LOGE(TAG, "🔴 SEND #%d [COMPLETE]: %s", s_send_count, buffer);
    esp_err_t ret = esp_websocket_client_send_bin(s_ws_client, buffer, len, pdMS_TO_TICKS(1000));
    ESP_LOGE(TAG, "🔴 SEND #%d [COMPLETE]: DONE (ret=%d) - waiting for conversation.audio.delta...", s_send_count, ret);
    return ret;
}

esp_err_t coze_ws_cancel_response(void)
{
    if (!coze_ws_is_connected()) {
        return ESP_ERR_INVALID_STATE;
    }

    // NOTE: Coze protocol doesn't have a client-side cancel command
    // - Server sends "conversation.chat.canceled" event when it cancels a response
    // - Client cannot explicitly cancel, only clear local playback buffer
    // - In Coze, responses auto-generate after input_audio_buffer.complete
    ESP_LOGW(TAG, "⚠️  Cancel requested: Coze protocol auto-manages responses (no client cancel)");

    // Just return success - caller will handle clearing local audio buffers
    return ESP_OK;
}

esp_err_t coze_ws_create_response(void)
{
    if (!coze_ws_is_connected()) {
        ESP_LOGE(TAG, "❌ response.create: not connected (state=%d)", s_state);
        return ESP_ERR_INVALID_STATE;
    }

    // Build response.create message with modalities (OpenAI Realtime API format)
    // Format: {"type": "response.create", "response": {"modalities": ["audio", "text"]}}
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return ESP_FAIL;
    }

    cJSON_AddStringToObject(root, "type", "response.create");

    // Add response object with modalities (may be required by Coze API)
    cJSON *response = cJSON_AddObjectToObject(root, "response");
    if (response) {
        cJSON *modalities = cJSON_CreateArray();
        cJSON_AddItemToArray(modalities, cJSON_CreateString("audio"));
        cJSON_AddItemToArray(modalities, cJSON_CreateString("text"));
        cJSON_AddItemToObject(response, "modalities", modalities);
    }

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (json_str == NULL) {
        return ESP_FAIL;
    }

    int len = strlen(json_str);
    s_send_count++;
    ESP_LOGE(TAG, "🔴 SEND #%d [RESPONSE]: response.create (%d bytes)", s_send_count, len);
    ESP_LOGE(TAG, "🔴 SEND #%d [RESPONSE]: %s", s_send_count, json_str);

    // Check WebSocket connection before sending
    bool ws_connected = esp_websocket_client_is_connected(s_ws_client);
    ESP_LOGE(TAG, "🔴 SEND #%d [RESPONSE]: ws_connected=%d, state=%d", s_send_count, ws_connected, s_state);

    esp_err_t ret = esp_websocket_client_send_bin(s_ws_client, json_str, len, pdMS_TO_TICKS(1000));
    ESP_LOGE(TAG, "🔴 SEND #%d [RESPONSE]: DONE (ret=%d) - now waiting for server response...", s_send_count, ret);
    free(json_str);

    return ret;
}

esp_err_t coze_ws_register_callback(coze_event_callback_t callback, void *user_data)
{
    s_event_callback = callback;
    s_callback_user_data = user_data;
    return ESP_OK;
}

const char *coze_ws_get_session_id(void)
{
    return s_session_id[0] ? s_session_id : NULL;
}

esp_err_t coze_ws_start_task(void)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "Cannot start task - not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (s_task_running) {
        return ESP_OK;
    }

    size_t spiram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    ESP_LOGI(TAG, "Starting Coze WS task (stack=%d, SPIRAM=%u, internal=%u)",
             WS_TASK_STACK_SIZE, (unsigned)spiram_free, (unsigned)internal_free);

    s_task_running = true;

    // ⚠️ CRITICAL: WebSocket + TLS + JSON requires internal RAM for stack!
    // PSRAM cannot safely hold function pointers and return addresses
    // Deep call chains (TLS callbacks, JSON parsing) will corrupt PSRAM stack
    BaseType_t ret = xTaskCreatePinnedToCore(
        coze_ws_task,
        "coze_ws",
        WS_TASK_STACK_SIZE,
        NULL,
        10,
        &s_ws_task,
        1  // Pin to Core 1 for better performance
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create task! SPIRAM=%u, internal=%u",
                 (unsigned)spiram_free, (unsigned)internal_free);
        s_task_running = false;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Coze WS task started");
    return ESP_OK;
}

esp_err_t coze_ws_stop_task(void)
{
    if (!s_task_running) {
        return ESP_OK;
    }

    s_task_running = false;
    vTaskDelay(pdMS_TO_TICKS(200));
    s_ws_task = NULL;

    ESP_LOGI(TAG, "Coze WS task stopped");
    return ESP_OK;
}

const char *coze_ws_msg_type_to_string(coze_msg_type_t type)
{
    switch (type) {
        case COZE_MSG_TYPE_SPEECH_CREATED: return "SPEECH_CREATED";
        case COZE_MSG_TYPE_SESSION_UPDATED: return "SESSION_UPDATED";
        case COZE_MSG_TYPE_INPUT_AUDIO_BUFFER_SPEECH_STARTED: return "SPEECH_STARTED";
        case COZE_MSG_TYPE_INPUT_AUDIO_BUFFER_SPEECH_STOPPED: return "SPEECH_STOPPED";
        case COZE_MSG_TYPE_RESPONSE_CREATED: return "RESPONSE_CREATED";
        case COZE_MSG_TYPE_RESPONSE_AUDIO_TRANSCRIPT_DELTA: return "TRANSCRIPT_DELTA";
        case COZE_MSG_TYPE_RESPONSE_AUDIO_DELTA: return "AUDIO_DELTA";
        case COZE_MSG_TYPE_RESPONSE_AUDIO_DONE: return "AUDIO_DONE";
        case COZE_MSG_TYPE_RESPONSE_DONE: return "RESPONSE_DONE";
        case COZE_MSG_TYPE_ERROR: return "ERROR";
        default: return "UNKNOWN";
    }
}

const char *coze_ws_state_to_string(coze_state_t state)
{
    switch (state) {
        case COZE_STATE_DISCONNECTED: return "DISCONNECTED";
        case COZE_STATE_CONNECTING: return "CONNECTING";
        case COZE_STATE_CONNECTED: return "CONNECTED";
        case COZE_STATE_AUTHENTICATING: return "AUTHENTICATING";
        case COZE_STATE_READY: return "READY";
        case COZE_STATE_STREAMING: return "STREAMING";
        case COZE_STATE_ERROR: return "ERROR";
        default: return "UNKNOWN";
    }
}
