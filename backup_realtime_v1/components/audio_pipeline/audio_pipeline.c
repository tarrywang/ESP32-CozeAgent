/**
 * @file audio_pipeline.c
 * @brief Audio pipeline manager implementation
 */

#include "audio_pipeline.h"
#include "audio_recorder.h"
#include "audio_player.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

static const char *TAG = "AUDIO_PIPELINE";

// ============================================
// Private Variables
// ============================================

static bool s_initialized = false;
static audio_pipeline_state_t s_state = AUDIO_PIPELINE_STATE_IDLE;
static audio_pipeline_config_t s_config;

// Audio queues for data transfer
static QueueHandle_t s_record_queue = NULL;
static QueueHandle_t s_playback_queue = NULL;

// Synchronization
static SemaphoreHandle_t s_pipeline_mutex = NULL;

// Recording pipeline task
static TaskHandle_t s_record_pipeline_task = NULL;
static volatile bool s_record_task_running = false;

// ============================================
// Audio Frame Structure
// ============================================

typedef struct {
    uint8_t data[AUDIO_FRAME_BYTES];
    size_t size;
    vad_state_t vad_state;
    uint32_t timestamp;
} audio_frame_t;

// ============================================
// Recording Pipeline Task
// ============================================

/**
 * @brief Recording pipeline task - reads from recorder and calls callback
 */
static void record_pipeline_task(void *pvParameters)
{
    ESP_LOGI(TAG, "📢 Recording pipeline task STARTED");

    uint8_t *audio_buffer = heap_caps_malloc(AUDIO_FRAME_BYTES, MALLOC_CAP_DMA);
    if (audio_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate audio buffer");
        vTaskDelete(NULL);
        return;
    }

    uint32_t frame_count = 0;
    uint32_t last_log_time = 0;

    while (s_record_task_running) {
        size_t bytes_read = 0;

        // Read from recorder ring buffer
        esp_err_t ret = audio_recorder_read(audio_buffer, AUDIO_FRAME_BYTES, &bytes_read, 50);

        if (ret == ESP_OK && bytes_read > 0) {
            frame_count++;
            vad_state_t vad_state = audio_recorder_get_vad_state();

            // Log every 50 frames (~1 second at 20ms/frame)
            uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
            if (now - last_log_time >= 1000) {
                ESP_LOGI(TAG, "📢 Pipeline: %lu frames read, last=%u bytes, VAD=%d, cb=%p",
                         frame_count, (unsigned)bytes_read, vad_state, s_config.record_cb);
                last_log_time = now;
            }

            // Call the record callback if registered
            if (s_config.record_cb) {
                s_config.record_cb(audio_buffer, bytes_read, vad_state, s_config.user_data);
            } else {
                ESP_LOGW(TAG, "⚠️ No record callback registered!");
            }

            // Also queue for internal use
            audio_frame_t frame;
            memcpy(frame.data, audio_buffer, bytes_read);
            frame.size = bytes_read;
            frame.vad_state = vad_state;
            frame.timestamp = xTaskGetTickCount();
            xQueueSend(s_record_queue, &frame, 0);
        }
    }

    free(audio_buffer);
    ESP_LOGI(TAG, "📢 Recording pipeline task STOPPED (total frames: %lu)", frame_count);
    vTaskDelete(NULL);
}

// ============================================
// Public Functions
// ============================================

esp_err_t audio_pipeline_init(void)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "Audio pipeline already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing audio pipeline...");

    // Initialize with default config
    s_config = (audio_pipeline_config_t)AUDIO_PIPELINE_DEFAULT_CONFIG();

    // Create mutex
    s_pipeline_mutex = xSemaphoreCreateMutex();
    if (s_pipeline_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create pipeline mutex");
        return ESP_ERR_NO_MEM;
    }

    // Create audio queues (use PSRAM if available)
    s_record_queue = xQueueCreate(20, sizeof(audio_frame_t));
    s_playback_queue = xQueueCreate(20, sizeof(audio_frame_t));

    if (s_record_queue == NULL || s_playback_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create audio queues");
        return ESP_ERR_NO_MEM;
    }

    // Initialize recorder
    audio_recorder_config_t rec_config = AUDIO_RECORDER_DEFAULT_CONFIG();
    rec_config.enable_aec = s_config.enable_aec;
    rec_config.enable_ns = s_config.enable_ns;
    rec_config.enable_vad = s_config.enable_vad;

    esp_err_t ret = audio_recorder_init(&rec_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize recorder: %s", esp_err_to_name(ret));
        return ret;
    }

    // Initialize player
    audio_player_config_t play_config = AUDIO_PLAYER_DEFAULT_CONFIG();
    play_config.sample_rate = s_config.sample_rate;
    play_config.bits_per_sample = s_config.bits_per_sample;
    play_config.channels = s_config.channels;

    ret = audio_player_init(&play_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize player: %s", esp_err_to_name(ret));
        return ret;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "Audio pipeline initialized");

    return ESP_OK;
}

esp_err_t audio_pipeline_deinit(void)
{
    if (!s_initialized) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Deinitializing audio pipeline...");

    // Stop tasks first
    audio_pipeline_stop_tasks();

    // Deinitialize components
    audio_recorder_deinit();
    audio_player_deinit();

    // Delete queues
    if (s_record_queue) {
        vQueueDelete(s_record_queue);
        s_record_queue = NULL;
    }
    if (s_playback_queue) {
        vQueueDelete(s_playback_queue);
        s_playback_queue = NULL;
    }

    // Delete mutex
    if (s_pipeline_mutex) {
        vSemaphoreDelete(s_pipeline_mutex);
        s_pipeline_mutex = NULL;
    }

    s_initialized = false;
    ESP_LOGI(TAG, "Audio pipeline deinitialized");

    return ESP_OK;
}

esp_err_t audio_pipeline_configure(const audio_pipeline_config_t *config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_pipeline_mutex, portMAX_DELAY);
    s_config = *config;
    xSemaphoreGive(s_pipeline_mutex);

    return ESP_OK;
}

esp_err_t audio_pipeline_start_recording(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_pipeline_mutex, portMAX_DELAY);

    esp_err_t ret = audio_recorder_start();
    if (ret == ESP_OK) {
        // Start the recording pipeline task to read audio and call callback
        if (!s_record_task_running) {
            s_record_task_running = true;
            BaseType_t task_ret = xTaskCreatePinnedToCoreWithCaps(
                record_pipeline_task,
                "rec_pipe",
                8192,   // Increased to 8192: audio_frame_t (1920 bytes) on stack + callback chain
                NULL,
                17,     // High priority, below recorder
                &s_record_pipeline_task,
                1,      // Core 1
                MALLOC_CAP_SPIRAM
            );

            if (task_ret != pdPASS) {
                ESP_LOGE(TAG, "Failed to create recording pipeline task");
                s_record_task_running = false;
                audio_recorder_stop();
                xSemaphoreGive(s_pipeline_mutex);
                return ESP_FAIL;
            }
            ESP_LOGI(TAG, "Recording pipeline task created");
        }

        s_state = (s_state == AUDIO_PIPELINE_STATE_PLAYING) ?
                   AUDIO_PIPELINE_STATE_FULL_DUPLEX : AUDIO_PIPELINE_STATE_RECORDING;

        if (s_config.event_cb) {
            s_config.event_cb(s_state, s_config.user_data);
        }
    }

    xSemaphoreGive(s_pipeline_mutex);
    return ret;
}

esp_err_t audio_pipeline_stop_recording(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_pipeline_mutex, portMAX_DELAY);

    // Stop the recording pipeline task first
    if (s_record_task_running) {
        s_record_task_running = false;
        vTaskDelay(pdMS_TO_TICKS(100));  // Wait for task to finish
        s_record_pipeline_task = NULL;
        ESP_LOGI(TAG, "Recording pipeline task stopped");
    }

    esp_err_t ret = audio_recorder_stop();
    if (ret == ESP_OK) {
        s_state = (s_state == AUDIO_PIPELINE_STATE_FULL_DUPLEX) ?
                   AUDIO_PIPELINE_STATE_PLAYING : AUDIO_PIPELINE_STATE_IDLE;

        if (s_config.event_cb) {
            s_config.event_cb(s_state, s_config.user_data);
        }
    }

    xSemaphoreGive(s_pipeline_mutex);
    return ret;
}

esp_err_t audio_pipeline_start_playback(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_pipeline_mutex, portMAX_DELAY);

    esp_err_t ret = audio_player_start();
    if (ret == ESP_OK) {
        s_state = (s_state == AUDIO_PIPELINE_STATE_RECORDING) ?
                   AUDIO_PIPELINE_STATE_FULL_DUPLEX : AUDIO_PIPELINE_STATE_PLAYING;

        if (s_config.event_cb) {
            s_config.event_cb(s_state, s_config.user_data);
        }
    }

    xSemaphoreGive(s_pipeline_mutex);
    return ret;
}

esp_err_t audio_pipeline_stop_playback(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_pipeline_mutex, portMAX_DELAY);

    esp_err_t ret = audio_player_stop();
    if (ret == ESP_OK) {
        s_state = (s_state == AUDIO_PIPELINE_STATE_FULL_DUPLEX) ?
                   AUDIO_PIPELINE_STATE_RECORDING : AUDIO_PIPELINE_STATE_IDLE;

        if (s_config.event_cb) {
            s_config.event_cb(s_state, s_config.user_data);
        }
    }

    xSemaphoreGive(s_pipeline_mutex);
    return ret;
}

int audio_pipeline_write(const uint8_t *data, size_t size, uint32_t timeout_ms)
{
    if (!s_initialized || data == NULL || s_playback_queue == NULL) {
        return -1;
    }

    // Break into frames and queue
    size_t total_written = 0;
    size_t remaining = size;
    const uint8_t *ptr = data;

    while (remaining > 0) {
        audio_frame_t frame;
        size_t chunk = (remaining > AUDIO_FRAME_BYTES) ? AUDIO_FRAME_BYTES : remaining;

        memcpy(frame.data, ptr, chunk);
        frame.size = chunk;
        frame.vad_state = VAD_STATE_SILENCE;  // N/A for playback
        frame.timestamp = xTaskGetTickCount();

        if (xQueueSend(s_playback_queue, &frame, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
            break;
        }

        ptr += chunk;
        remaining -= chunk;
        total_written += chunk;
    }

    return total_written;
}

int audio_pipeline_read(uint8_t *data, size_t size, uint32_t timeout_ms)
{
    if (!s_initialized || data == NULL || s_record_queue == NULL) {
        return -1;
    }

    audio_frame_t frame;

    if (xQueueReceive(s_record_queue, &frame, pdMS_TO_TICKS(timeout_ms)) == pdTRUE) {
        size_t copy_size = (frame.size > size) ? size : frame.size;
        memcpy(data, frame.data, copy_size);
        return copy_size;
    }

    return 0;
}

audio_pipeline_state_t audio_pipeline_get_state(void)
{
    return s_state;
}

vad_state_t audio_pipeline_get_vad_state(void)
{
    return audio_recorder_get_vad_state();
}

bool audio_pipeline_is_voice_active(void)
{
    vad_state_t vad = audio_recorder_get_vad_state();
    return (vad == VAD_STATE_VOICE_START || vad == VAD_STATE_VOICE);
}

esp_err_t audio_pipeline_set_volume(uint8_t volume)
{
    return audio_player_set_volume(volume);
}

uint8_t audio_pipeline_get_volume(void)
{
    return audio_player_get_volume();
}

esp_err_t audio_pipeline_set_mute(bool mute)
{
    return audio_player_set_mute(mute);
}

esp_err_t audio_pipeline_clear_playback_buffer(void)
{
    if (s_playback_queue) {
        xQueueReset(s_playback_queue);
    }
    return audio_player_clear_buffer();
}

esp_err_t audio_pipeline_start_tasks(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    // Note: The actual audio tasks are managed internally by:
    // - audio_recorder: creates recorder_task in audio_recorder_start()
    // - audio_player: creates player_task in audio_player_init()
    // No additional pipeline tasks needed - this avoids duplicate task creation
    // and reduces internal RAM usage for task stacks.

    ESP_LOGI(TAG, "Audio pipeline ready (tasks managed by recorder/player)");
    return ESP_OK;
}

esp_err_t audio_pipeline_stop_tasks(void)
{
    // Note: The actual audio tasks are managed internally by:
    // - audio_recorder: stops recorder_task in audio_recorder_stop()
    // - audio_player: stops player_task in audio_player_stop()
    // No pipeline-level task cleanup needed.

    ESP_LOGI(TAG, "Audio pipeline tasks cleanup (managed by recorder/player)");
    return ESP_OK;
}

QueueHandle_t audio_pipeline_get_record_queue(void)
{
    return s_record_queue;
}

QueueHandle_t audio_pipeline_get_playback_queue(void)
{
    return s_playback_queue;
}

esp_err_t audio_pipeline_enable_aec(bool enable)
{
    s_config.enable_aec = enable;
    // TODO: Update recorder AEC setting at runtime
    return ESP_OK;
}

esp_err_t audio_pipeline_enable_ns(bool enable)
{
    s_config.enable_ns = enable;
    // TODO: Update recorder NS setting at runtime
    return ESP_OK;
}

esp_err_t audio_pipeline_enable_vad(bool enable)
{
    s_config.enable_vad = enable;
    // TODO: Update recorder VAD setting at runtime
    return ESP_OK;
}
