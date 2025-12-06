/**
 * @file app_core.c
 * @brief Application core state machine implementation
 */

#include "app_core.h"
#include "app_events.h"
#include "audio_pipeline.h"
#include "coze_ws.h"
#include "azure_realtime.h"
#include "ui_manager.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

static const char *TAG = "APP_CORE";

// ============================================
// External declarations
// ============================================
extern lv_display_t *app_get_display(void);

// ============================================
// Private Variables
// ============================================

static bool s_initialized = false;
static app_state_t s_current_state = APP_STATE_INIT;
static SemaphoreHandle_t s_state_mutex = NULL;

// Task
static TaskHandle_t s_core_task = NULL;
static volatile bool s_task_running = false;

// Callback
static app_state_callback_t s_state_callback = NULL;
static void *s_callback_user_data = NULL;

// Accumulated transcript
static char s_user_transcript[1024] = {0};
static char s_ai_transcript[2048] = {0};

// ============================================
// Private Functions
// ============================================

/**
 * @brief Audio data callback from recording pipeline
 */
static void audio_record_callback(const uint8_t *data, size_t size,
                                   vad_state_t vad_state, void *user_data)
{
    // Send audio to Azure
    if (s_current_state == APP_STATE_LISTENING) {
        ESP_LOGI(TAG, "🎤 Audio callback: %u bytes, VAD=%d, state=%s",
                 (unsigned)size, vad_state, app_core_state_to_string(s_current_state));
        esp_err_t ret = azure_realtime_send_audio(data, size);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "❌ Failed to send audio: %s", esp_err_to_name(ret));
        }

        // Update UI with audio level
        uint8_t level = audio_pipeline_get_volume();
        ui_manager_update_audio_level(level);

        // Handle VAD events
        if (vad_state == VAD_STATE_VOICE_END) {
            ESP_LOGI(TAG, "🎤 VAD: Voice END detected");
            app_core_send_event(APP_EVENT_VOICE_END);
        } else if (vad_state == VAD_STATE_VOICE_START) {
            ESP_LOGI(TAG, "🎤 VAD: Voice START detected");
            app_core_send_event(APP_EVENT_VOICE_START);
        }
    }
}

/**
 * @brief Coze event callback
 */
static void coze_event_callback(const coze_event_t *event, void *user_data)
{
    ESP_LOGI(TAG, "🔍 ENTRY: coze_event_callback (event=%p, type=%d)",
             event, event ? event->type : -1);

    if (event == NULL) {
        ESP_LOGE(TAG, "❌ NULL event pointer!");
        return;
    }

    ESP_LOGD(TAG, "🤖 Coze event: %s", coze_ws_msg_type_to_string(event->type));

    switch (event->type) {
        // Session created - ready for conversation
        case COZE_MSG_TYPE_SPEECH_CREATED:
            ESP_LOGI(TAG, "🤖 Coze: Speech session created");
            break;

        // Session updated - configuration acknowledged
        case COZE_MSG_TYPE_SESSION_UPDATED:
            ESP_LOGI(TAG, "🤖 Coze: Session updated");
            break;

        // VAD detected speech start
        case COZE_MSG_TYPE_INPUT_AUDIO_BUFFER_SPEECH_STARTED:
            ESP_LOGI(TAG, "🎤 Coze VAD: Speech started");
            break;

        // VAD detected speech end
        case COZE_MSG_TYPE_INPUT_AUDIO_BUFFER_SPEECH_STOPPED:
            ESP_LOGI(TAG, "🎤 Coze VAD: Speech stopped");
            break;

        // Response created - AI is generating response
        case COZE_MSG_TYPE_RESPONSE_CREATED:
            ESP_LOGI(TAG, "🤖 Coze: Response created, AI responding");
            app_core_send_event(APP_EVENT_COZE_RESPONSE_START);
            break;

        // Transcript delta - text transcript from AI
        case COZE_MSG_TYPE_RESPONSE_AUDIO_TRANSCRIPT_DELTA:
            if (event->text) {
                ESP_LOGI(TAG, "🤖 Coze transcript: \"%s\"", event->text);
                // Append to AI transcript
                strncat(s_ai_transcript, event->text,
                        sizeof(s_ai_transcript) - strlen(s_ai_transcript) - 1);
                if (app_get_display() != NULL) {
                    ui_manager_update_transcript(event->text, false);
                }
            }
            break;

        // Audio delta - streaming audio data from AI
        case COZE_MSG_TYPE_RESPONSE_AUDIO_DELTA:
            if (event->audio_data && event->audio_size > 0) {
                ESP_LOGD(TAG, "🔊 Coze audio: %u bytes", (unsigned)event->audio_size);
                // Write audio to playback pipeline
                audio_pipeline_write(event->audio_data, event->audio_size, 100);
            }
            break;

        // Audio done - AI finished sending audio
        case COZE_MSG_TYPE_RESPONSE_AUDIO_DONE:
            ESP_LOGI(TAG, "🔊 Coze: Audio stream done");
            break;

        // Response done - AI finished responding
        case COZE_MSG_TYPE_RESPONSE_DONE:
            ESP_LOGI(TAG, "🤖 Coze: Response DONE");
            ESP_LOGI(TAG, "📝 Full AI transcript: \"%s\"", s_ai_transcript);
            app_core_send_event(APP_EVENT_COZE_RESPONSE_END);
            break;

        // Error event
        case COZE_MSG_TYPE_ERROR:
            ESP_LOGE(TAG, "❌ Coze error: %s", event->error_message ? event->error_message : "Unknown");
            app_core_send_event(APP_EVENT_COZE_ERROR);
            break;

        default:
            ESP_LOGD(TAG, "Unhandled Coze event type: %d", event->type);
            break;
    }

    ESP_LOGI(TAG, "✅ EXIT: coze_event_callback");
}

/**
 * @brief Azure Realtime event callback
 */
static void azure_event_callback(const azure_event_t *event, void *user_data)
{
    ESP_LOGI(TAG, "🔍 ENTRY: azure_event_callback (event=%p, type=%d)",
             event, event ? event->type : -1);

    if (event == NULL) {
        ESP_LOGE(TAG, "❌ NULL event pointer!");
        return;
    }

    ESP_LOGD(TAG, "🤖 Azure event: %s", azure_realtime_msg_type_to_string(event->type));

    switch (event->type) {
        // Session created - ready for conversation
        case AZURE_MSG_TYPE_SESSION_CREATED:
            ESP_LOGI(TAG, "🤖 Azure: Session created");
            break;

        // Session updated - configuration acknowledged
        case AZURE_MSG_TYPE_SESSION_UPDATED:
            ESP_LOGI(TAG, "🤖 Azure: Session updated");
            break;

        // VAD detected speech start
        case AZURE_MSG_TYPE_INPUT_AUDIO_BUFFER_SPEECH_STARTED:
            ESP_LOGI(TAG, "🎤 Azure VAD: Speech started");
            break;

        // VAD detected speech end
        case AZURE_MSG_TYPE_INPUT_AUDIO_BUFFER_SPEECH_STOPPED:
            ESP_LOGI(TAG, "🎤 Azure VAD: Speech stopped");
            break;

        // Response created - AI is generating response
        case AZURE_MSG_TYPE_RESPONSE_CREATED:
            ESP_LOGI(TAG, "🤖 Azure: Response created, AI responding");
            app_core_send_event(APP_EVENT_COZE_RESPONSE_START);
            break;

        // Transcript delta - text transcript from AI
        case AZURE_MSG_TYPE_RESPONSE_AUDIO_TRANSCRIPT_DELTA:
            if (event->text) {
                ESP_LOGI(TAG, "🤖 Azure transcript: \"%s\"", event->text);
                // Append to AI transcript
                strncat(s_ai_transcript, event->text,
                        sizeof(s_ai_transcript) - strlen(s_ai_transcript) - 1);
                if (app_get_display() != NULL) {
                    ui_manager_update_transcript(event->text, false);
                }
            }
            break;

        // Audio delta - streaming audio data from AI
        case AZURE_MSG_TYPE_RESPONSE_AUDIO_DELTA:
            if (event->audio_data && event->audio_size > 0) {
                ESP_LOGD(TAG, "🔊 Azure audio: %u bytes", (unsigned)event->audio_size);
                // Write audio to playback pipeline
                audio_pipeline_write(event->audio_data, event->audio_size, 100);
            }
            break;

        // Audio done - AI finished sending audio
        case AZURE_MSG_TYPE_RESPONSE_AUDIO_DONE:
            ESP_LOGI(TAG, "🔊 Azure: Audio stream done");
            break;

        // Response done - AI finished responding
        case AZURE_MSG_TYPE_RESPONSE_DONE:
            ESP_LOGI(TAG, "🤖 Azure: Response DONE");
            ESP_LOGI(TAG, "📝 Full AI transcript: \"%s\"", s_ai_transcript);
            app_core_send_event(APP_EVENT_COZE_RESPONSE_END);
            break;

        // Error event
        case AZURE_MSG_TYPE_ERROR:
            ESP_LOGE(TAG, "❌ Azure error: %s", event->error_message ? event->error_message : "Unknown");
            app_core_send_event(APP_EVENT_COZE_ERROR);
            break;

        default:
            ESP_LOGD(TAG, "Unhandled Azure event type: %d", event->type);
            break;
    }

    ESP_LOGI(TAG, "✅ EXIT: azure_event_callback");
}

/**
 * @brief UI event callback
 */
static void ui_event_callback(ui_event_t event, void *user_data)
{
    switch (event) {
        case UI_EVENT_TAP:
            app_core_send_event(APP_EVENT_USER_TAP);
            break;

        case UI_EVENT_LONG_PRESS:
            app_core_send_event(APP_EVENT_USER_LONG_PRESS);
            break;

        case UI_EVENT_DOUBLE_TAP:
            app_core_send_event(APP_EVENT_CANCEL);
            break;

        default:
            break;
    }
}

/**
 * @brief Transition to new state
 */
static esp_err_t transition_to_state(app_state_t new_state)
{
    if (new_state == s_current_state) {
        ESP_LOGD(TAG, "Already in state %s, ignoring transition", app_core_state_to_string(new_state));
        return ESP_OK;
    }

    xSemaphoreTake(s_state_mutex, portMAX_DELAY);

    app_state_t old_state = s_current_state;
    ESP_LOGI(TAG, "🔄 State transition: %s -> %s",
             app_core_state_to_string(old_state),
             app_core_state_to_string(new_state));

    // Exit current state
    switch (old_state) {
        case APP_STATE_LISTENING:
            ESP_LOGI(TAG, "⬅️  Exiting LISTENING state, stopping recording");
            audio_pipeline_stop_recording();
            break;

        case APP_STATE_SPEAKING:
            ESP_LOGI(TAG, "⬅️  Exiting SPEAKING state, stopping playback");
            audio_pipeline_stop_playback();
            break;

        default:
            break;
    }

    // Enter new state
    switch (new_state) {
        case APP_STATE_IDLE:
            ESP_LOGI(TAG, "➡️  Entering IDLE state");
            if (app_get_display() != NULL) {
                ui_manager_set_page(UI_PAGE_IDLE);
            } else {
                ESP_LOGW(TAG, "Display not available, skipping UI update");
            }
            break;

        case APP_STATE_LISTENING:
            ESP_LOGI(TAG, "➡️  Entering LISTENING state, starting recording");
            // Clear transcripts
            s_user_transcript[0] = '\0';
            s_ai_transcript[0] = '\0';
            if (app_get_display() != NULL) {
                ui_manager_clear_transcript();
                ui_manager_set_page(UI_PAGE_LISTENING);
            }
            audio_pipeline_start_recording();
            break;

        case APP_STATE_PROCESSING:
            ESP_LOGI(TAG, "➡️  Entering PROCESSING state, completing audio (Azure manual mode)");
            if (app_get_display() != NULL) {
                ui_manager_set_page(UI_PAGE_THINKING);
            }
            // Complete audio buffer and request response (Azure requires manual mode)
            azure_realtime_commit_audio();
            azure_realtime_create_response();  // Azure manual mode: must explicitly request response
            break;

        case APP_STATE_SPEAKING:
            ESP_LOGI(TAG, "➡️  Entering SPEAKING state, starting playback");
            if (app_get_display() != NULL) {
                ui_manager_set_page(UI_PAGE_SPEAKING);
            }
            audio_pipeline_start_playback();
            break;

        case APP_STATE_ERROR:
            ESP_LOGE(TAG, "➡️  Entering ERROR state");
            if (app_get_display() != NULL) {
                ui_manager_set_page(UI_PAGE_ERROR);
            }
            break;

        default:
            break;
    }

    s_current_state = new_state;

    // Notify callback
    if (s_state_callback) {
        s_state_callback(old_state, new_state, s_callback_user_data);
    }

    xSemaphoreGive(s_state_mutex);
    return ESP_OK;
}

/**
 * @brief Process state machine event
 */
static void process_event(app_event_t event)
{
    ESP_LOGI(TAG, "📥 Processing event: %s in state: %s",
             app_core_event_to_string(event),
             app_core_state_to_string(s_current_state));

    switch (s_current_state) {
        case APP_STATE_IDLE:
            if (event == APP_EVENT_USER_TAP || event == APP_EVENT_BUTTON_PRESS) {
                ESP_LOGI(TAG, "👆 User triggered conversation start");
                if (azure_realtime_is_connected()) {
                    ESP_LOGI(TAG, "✅ Azure connected, starting conversation");
                    transition_to_state(APP_STATE_LISTENING);
                } else {
                    ESP_LOGW(TAG, "⚠️  Azure not connected!");
                    ui_manager_show_status("Not connected to server", false);
                }
            }
            break;

        case APP_STATE_LISTENING:
            if (event == APP_EVENT_USER_TAP || event == APP_EVENT_VOICE_END) {
                ESP_LOGI(TAG, "🎤 User finished speaking (tap or VAD end)");
                transition_to_state(APP_STATE_PROCESSING);
            } else if (event == APP_EVENT_CANCEL || event == APP_EVENT_USER_LONG_PRESS) {
                ESP_LOGI(TAG, "❌ User cancelled recording");
                transition_to_state(APP_STATE_IDLE);
            }
            break;

        case APP_STATE_PROCESSING:
            if (event == APP_EVENT_COZE_RESPONSE_START) {
                ESP_LOGI(TAG, "🤖 Azure started responding");
                transition_to_state(APP_STATE_SPEAKING);
            } else if (event == APP_EVENT_COZE_ERROR) {
                ESP_LOGE(TAG, "❌ Azure returned error");
                ui_manager_show_error("Failed to get response");
                transition_to_state(APP_STATE_ERROR);
            } else if (event == APP_EVENT_CANCEL) {
                ESP_LOGI(TAG, "❌ User cancelled processing");
                azure_realtime_cancel_response();
                transition_to_state(APP_STATE_IDLE);
            }
            break;

        case APP_STATE_SPEAKING:
            if (event == APP_EVENT_COZE_RESPONSE_END || event == APP_EVENT_AUDIO_DONE) {
                ESP_LOGI(TAG, "🔊 Playback finished, returning to IDLE");
                // Check if user wants to continue conversation
                transition_to_state(APP_STATE_IDLE);
            } else if (event == APP_EVENT_USER_TAP || event == APP_EVENT_CANCEL) {
                ESP_LOGI(TAG, "❌ User interrupted playback");
                // Interrupt playback
                azure_realtime_cancel_response();
                audio_pipeline_clear_playback_buffer();
                transition_to_state(APP_STATE_IDLE);
            }
            break;

        case APP_STATE_ERROR:
            if (event == APP_EVENT_USER_TAP) {
                ESP_LOGI(TAG, "👆 User acknowledged error, returning to IDLE");
                transition_to_state(APP_STATE_IDLE);
            }
            break;

        default:
            break;
    }
}

/**
 * @brief Application core task
 */
static void app_core_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Application core task started");

    app_event_msg_t event;

    while (s_task_running) {
        // Wait for events
        if (app_events_receive(&event, 100) == ESP_OK) {
            process_event((app_event_t)event.type);
        }
    }

    ESP_LOGI(TAG, "Application core task stopped");
    vTaskDelete(NULL);
}

// ============================================
// Public Functions
// ============================================

esp_err_t app_core_init(void)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "App core already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing application core...");

    // Create mutex
    s_state_mutex = xSemaphoreCreateMutex();
    if (s_state_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create state mutex");
        return ESP_ERR_NO_MEM;
    }

    // Initialize event system
    esp_err_t ret = app_events_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init event system");
        return ret;
    }

    // Register callbacks
    audio_pipeline_config_t audio_cfg = AUDIO_PIPELINE_DEFAULT_CONFIG();
    audio_cfg.record_cb = audio_record_callback;
    audio_pipeline_configure(&audio_cfg);

    azure_realtime_register_callback(azure_event_callback, NULL);
    ui_manager_register_callback(ui_event_callback, NULL);

    s_current_state = APP_STATE_INIT;
    s_initialized = true;

    ESP_LOGI(TAG, "Application core initialized");
    return ESP_OK;
}

esp_err_t app_core_deinit(void)
{
    if (!s_initialized) {
        return ESP_OK;
    }

    app_core_stop_task();
    app_events_deinit();

    if (s_state_mutex) {
        vSemaphoreDelete(s_state_mutex);
        s_state_mutex = NULL;
    }

    s_initialized = false;
    ESP_LOGI(TAG, "Application core deinitialized");
    return ESP_OK;
}

esp_err_t app_core_start_task(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (s_task_running) {
        return ESP_OK;
    }

    s_task_running = true;

    // Use PSRAM for task stack to save internal RAM
    BaseType_t ret = xTaskCreatePinnedToCoreWithCaps(
        app_core_task,
        "app_core",
        4096,
        NULL,
        5,
        &s_core_task,
        0,  // Core 0
        MALLOC_CAP_SPIRAM
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create app core task");
        s_task_running = false;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "App core task started");
    return ESP_OK;
}

esp_err_t app_core_stop_task(void)
{
    if (!s_task_running) {
        return ESP_OK;
    }

    s_task_running = false;
    vTaskDelay(pdMS_TO_TICKS(200));
    s_core_task = NULL;

    ESP_LOGI(TAG, "App core task stopped");
    return ESP_OK;
}

app_state_t app_core_get_state(void)
{
    return s_current_state;
}

esp_err_t app_core_set_state(app_state_t state)
{
    return transition_to_state(state);
}

esp_err_t app_core_send_event(app_event_t event)
{
    app_event_msg_t msg = {
        .type = (uint32_t)event,
        .timestamp = xTaskGetTickCount(),
        .data = {0}
    };
    return app_events_post(&msg, 100);
}

esp_err_t app_core_register_callback(app_state_callback_t callback, void *user_data)
{
    s_state_callback = callback;
    s_callback_user_data = user_data;
    return ESP_OK;
}

const char *app_core_state_to_string(app_state_t state)
{
    switch (state) {
        case APP_STATE_INIT: return "INIT";
        case APP_STATE_IDLE: return "IDLE";
        case APP_STATE_LISTENING: return "LISTENING";
        case APP_STATE_PROCESSING: return "PROCESSING";
        case APP_STATE_SPEAKING: return "SPEAKING";
        case APP_STATE_ERROR: return "ERROR";
        default: return "UNKNOWN";
    }
}

const char *app_core_event_to_string(app_event_t event)
{
    switch (event) {
        case APP_EVENT_NONE: return "NONE";
        case APP_EVENT_USER_TAP: return "USER_TAP";
        case APP_EVENT_USER_LONG_PRESS: return "USER_LONG_PRESS";
        case APP_EVENT_BUTTON_PRESS: return "BUTTON_PRESS";
        case APP_EVENT_VOICE_START: return "VOICE_START";
        case APP_EVENT_VOICE_END: return "VOICE_END";
        case APP_EVENT_COZE_RESPONSE_START: return "COZE_RESPONSE_START";
        case APP_EVENT_COZE_RESPONSE_END: return "COZE_RESPONSE_END";
        case APP_EVENT_COZE_ERROR: return "COZE_ERROR";
        case APP_EVENT_AUDIO_DONE: return "AUDIO_DONE";
        case APP_EVENT_WIFI_CONNECTED: return "WIFI_CONNECTED";
        case APP_EVENT_WIFI_DISCONNECTED: return "WIFI_DISCONNECTED";
        case APP_EVENT_CANCEL: return "CANCEL";
        default: return "UNKNOWN";
    }
}

esp_err_t app_core_start_listening(void)
{
    if (s_current_state != APP_STATE_IDLE) {
        return ESP_ERR_INVALID_STATE;
    }
    return transition_to_state(APP_STATE_LISTENING);
}

esp_err_t app_core_stop_listening(void)
{
    if (s_current_state != APP_STATE_LISTENING) {
        return ESP_ERR_INVALID_STATE;
    }
    return transition_to_state(APP_STATE_PROCESSING);
}

esp_err_t app_core_cancel(void)
{
    return app_core_send_event(APP_EVENT_CANCEL);
}

bool app_core_is_ready(void)
{
    return s_initialized &&
           s_current_state == APP_STATE_IDLE &&
           azure_realtime_is_connected();
}
