/**
 * @file audio_player.c
 * @brief Audio player implementation for TTS playback
 *
 * Uses ESP codec device API from official Waveshare BSP.
 */

#include "audio_player.h"

// Use official BSP codec dev API
#include "esp_codec_dev.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/ringbuf.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

static const char *TAG = "AUDIO_PLAYER";

// ============================================
// External declarations from app_main
// ============================================
extern esp_codec_dev_handle_t app_get_speaker_codec(void);

// ============================================
// Configuration
// ============================================

#define AUDIO_SAMPLE_RATE       16000   // 16kHz for voice
#define AUDIO_FRAME_MS          60      // 60ms frame size (reduced from 20ms to lower WebSocket pressure)
#define AUDIO_FRAME_SAMPLES     (AUDIO_SAMPLE_RATE * AUDIO_FRAME_MS / 1000)
#define AUDIO_FRAME_BYTES       (AUDIO_FRAME_SAMPLES * 2)  // 16-bit samples = 2 bytes

#define PLAYER_RING_BUFFER_SIZE     (4096 * 10)  // ~640ms buffer at 16kHz mono
#define PLAYER_TASK_STACK_SIZE      4096

// ============================================
// Private Variables
// ============================================

static bool s_initialized = false;
static audio_player_state_t s_state = AUDIO_PLAYER_STATE_IDLE;
static audio_player_config_t s_config;

// Ring buffer for audio data
static RingbufHandle_t s_ring_buffer = NULL;

// Playback control
static bool s_muted = false;
static uint8_t s_volume = 80;

// Callback
static audio_player_callback_t s_callback = NULL;
static void *s_callback_user_data = NULL;

// Task
static TaskHandle_t s_player_task = NULL;
static volatile bool s_task_running = false;

// Event for playback finished
static SemaphoreHandle_t s_finish_sem = NULL;

// Mutex
static SemaphoreHandle_t s_mutex = NULL;

// Codec handle
static esp_codec_dev_handle_t s_spk_codec = NULL;
static bool s_codec_opened = false;

// ============================================
// Private Functions
// ============================================

/**
 * @brief Apply volume to audio samples
 */
static void apply_volume(int16_t *samples, size_t count, uint8_t volume)
{
    if (volume >= 100) return;

    for (size_t i = 0; i < count; i++) {
        samples[i] = (int16_t)((int32_t)samples[i] * volume / 100);
    }
}

/**
 * @brief Player task - reads from ring buffer, writes to codec
 */
static void player_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Player task started");

    // Allocate output buffer
    int16_t *output_buffer = heap_caps_malloc(AUDIO_FRAME_BYTES, MALLOC_CAP_DMA);
    if (output_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate output buffer");
        vTaskDelete(NULL);
        return;
    }

    while (s_task_running) {
        if (s_state == AUDIO_PLAYER_STATE_PLAYING && s_codec_opened) {
            size_t received_size;

            // Try to receive data from ring buffer
            void *data = xRingbufferReceiveUpTo(s_ring_buffer, &received_size,
                                                 pdMS_TO_TICKS(50), AUDIO_FRAME_BYTES);

            if (data != NULL && received_size > 0) {
                // Copy to output buffer
                memcpy(output_buffer, data, received_size);
                vRingbufferReturnItem(s_ring_buffer, data);

                size_t sample_count = received_size / sizeof(int16_t);

                // Apply volume if not muted
                if (!s_muted) {
                    apply_volume(output_buffer, sample_count, s_volume);
                } else {
                    memset(output_buffer, 0, received_size);
                }

                // Write to speaker codec
                esp_codec_dev_write(s_spk_codec, output_buffer, received_size);

            } else {
                // No data available - check if we're done
                // Give a small delay to allow more data to arrive
                vTaskDelay(pdMS_TO_TICKS(10));

                // Check buffer again
                size_t items_waiting = xRingbufferGetCurFreeSize(s_ring_buffer);
                if (items_waiting >= PLAYER_RING_BUFFER_SIZE - 100) {
                    // Buffer is essentially empty
                    // Write silence to flush any remaining audio
                    memset(output_buffer, 0, AUDIO_FRAME_BYTES);
                    esp_codec_dev_write(s_spk_codec, output_buffer, AUDIO_FRAME_BYTES);
                }
            }
        } else if (s_state == AUDIO_PLAYER_STATE_PAUSED) {
            // Paused - just wait
            vTaskDelay(pdMS_TO_TICKS(50));
        } else {
            // Idle - wait longer
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }

    free(output_buffer);
    ESP_LOGI(TAG, "Player task stopped");
    vTaskDelete(NULL);
}

// ============================================
// Public Functions
// ============================================

esp_err_t audio_player_init(const audio_player_config_t *config)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "Player already initialized");
        return ESP_OK;
    }

    // Use default config if not provided
    if (config == NULL) {
        s_config = (audio_player_config_t)AUDIO_PLAYER_DEFAULT_CONFIG();
    } else {
        s_config = *config;
    }

    s_volume = s_config.volume;

    ESP_LOGI(TAG, "Initializing audio player (%lu Hz, %d-bit, %d ch)",
             s_config.sample_rate, s_config.bits_per_sample, s_config.channels);

    // Get speaker codec handle from app_main
    s_spk_codec = app_get_speaker_codec();
    if (s_spk_codec == NULL) {
        ESP_LOGE(TAG, "Speaker codec not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    // Create ring buffer
    s_ring_buffer = xRingbufferCreate(PLAYER_RING_BUFFER_SIZE, RINGBUF_TYPE_BYTEBUF);
    if (s_ring_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to create ring buffer");
        return ESP_ERR_NO_MEM;
    }

    // Create mutex
    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }

    // Create finish semaphore
    s_finish_sem = xSemaphoreCreateBinary();
    if (s_finish_sem == NULL) {
        ESP_LOGE(TAG, "Failed to create finish semaphore");
        return ESP_ERR_NO_MEM;
    }

    // Start player task (use PSRAM for stack to save internal RAM)
    s_task_running = true;
    BaseType_t ret = xTaskCreatePinnedToCoreWithCaps(
        player_task,
        "audio_play",
        PLAYER_TASK_STACK_SIZE,
        NULL,
        17,     // High priority
        &s_player_task,
        1,      // Core 1
        MALLOC_CAP_SPIRAM
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create player task");
        s_task_running = false;
        return ESP_FAIL;
    }

    s_state = AUDIO_PLAYER_STATE_IDLE;
    s_initialized = true;

    ESP_LOGI(TAG, "Audio player initialized");
    return ESP_OK;
}

esp_err_t audio_player_deinit(void)
{
    if (!s_initialized) {
        return ESP_OK;
    }

    // Stop task
    s_task_running = false;
    vTaskDelay(pdMS_TO_TICKS(150));

    // Close codec if open
    if (s_codec_opened && s_spk_codec) {
        esp_codec_dev_close(s_spk_codec);
        s_codec_opened = false;
    }

    // Free resources
    if (s_ring_buffer) {
        vRingbufferDelete(s_ring_buffer);
        s_ring_buffer = NULL;
    }

    if (s_mutex) {
        vSemaphoreDelete(s_mutex);
        s_mutex = NULL;
    }

    if (s_finish_sem) {
        vSemaphoreDelete(s_finish_sem);
        s_finish_sem = NULL;
    }

    s_player_task = NULL;
    s_spk_codec = NULL;
    s_initialized = false;

    ESP_LOGI(TAG, "Audio player deinitialized");
    return ESP_OK;
}

esp_err_t audio_player_start(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    // Open speaker codec if not already open
    if (!s_codec_opened && s_spk_codec) {
        esp_codec_dev_sample_info_t fs = {
            .sample_rate = s_config.sample_rate,
            .channel = s_config.channels,
            .bits_per_sample = s_config.bits_per_sample,
        };

        if (esp_codec_dev_open(s_spk_codec, &fs) == ESP_CODEC_DEV_OK) {
            s_codec_opened = true;
            // Set initial volume
            esp_codec_dev_set_out_vol(s_spk_codec, s_volume);
        } else {
            ESP_LOGE(TAG, "Failed to open speaker codec");
            xSemaphoreGive(s_mutex);
            return ESP_FAIL;
        }
    }

    s_state = AUDIO_PLAYER_STATE_PLAYING;

    if (s_callback) {
        s_callback(s_state, s_callback_user_data);
    }

    xSemaphoreGive(s_mutex);

    ESP_LOGI(TAG, "Audio player started");
    return ESP_OK;
}

esp_err_t audio_player_stop(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    // Clear buffer
    xRingbufferGetCurFreeSize(s_ring_buffer);  // Force reset

    s_state = AUDIO_PLAYER_STATE_IDLE;

    // Close speaker codec
    if (s_codec_opened && s_spk_codec) {
        esp_codec_dev_close(s_spk_codec);
        s_codec_opened = false;
    }

    // Signal finish
    xSemaphoreGive(s_finish_sem);

    if (s_callback) {
        s_callback(s_state, s_callback_user_data);
    }

    xSemaphoreGive(s_mutex);

    ESP_LOGI(TAG, "Audio player stopped");
    return ESP_OK;
}

esp_err_t audio_player_pause(void)
{
    if (!s_initialized || s_state != AUDIO_PLAYER_STATE_PLAYING) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_state = AUDIO_PLAYER_STATE_PAUSED;

    if (s_callback) {
        s_callback(s_state, s_callback_user_data);
    }
    xSemaphoreGive(s_mutex);

    ESP_LOGI(TAG, "Audio player paused");
    return ESP_OK;
}

esp_err_t audio_player_resume(void)
{
    if (!s_initialized || s_state != AUDIO_PLAYER_STATE_PAUSED) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_state = AUDIO_PLAYER_STATE_PLAYING;

    if (s_callback) {
        s_callback(s_state, s_callback_user_data);
    }
    xSemaphoreGive(s_mutex);

    ESP_LOGI(TAG, "Audio player resumed");
    return ESP_OK;
}

bool audio_player_is_playing(void)
{
    return s_state == AUDIO_PLAYER_STATE_PLAYING;
}

audio_player_state_t audio_player_get_state(void)
{
    return s_state;
}

int audio_player_write(const uint8_t *data, size_t size, uint32_t timeout_ms)
{
    if (!s_initialized || data == NULL) {
        return -1;
    }

    // Send to ring buffer
    if (xRingbufferSend(s_ring_buffer, data, size, pdMS_TO_TICKS(timeout_ms)) == pdTRUE) {
        return size;
    }

    return 0;  // Buffer full
}

esp_err_t audio_player_write_blocking(const uint8_t *data, size_t size)
{
    if (!s_initialized || data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // Start playback if not already playing
    if (s_state != AUDIO_PLAYER_STATE_PLAYING) {
        audio_player_start();
    }

    // Write data
    size_t written = 0;
    while (written < size) {
        int ret = audio_player_write(data + written, size - written, 100);
        if (ret > 0) {
            written += ret;
        } else if (ret < 0) {
            return ESP_FAIL;
        }
        // If ret == 0, buffer is full, wait and retry
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    return ESP_OK;
}

esp_err_t audio_player_set_volume(uint8_t volume)
{
    if (volume > 100) volume = 100;
    s_volume = volume;

    // Also set codec volume if open
    if (s_codec_opened && s_spk_codec) {
        esp_codec_dev_set_out_vol(s_spk_codec, volume);
    }

    ESP_LOGI(TAG, "Volume set to %d%%", volume);
    return ESP_OK;
}

uint8_t audio_player_get_volume(void)
{
    return s_volume;
}

esp_err_t audio_player_set_mute(bool mute)
{
    s_muted = mute;

    // Mute via codec if available
    if (s_codec_opened && s_spk_codec) {
        esp_codec_dev_set_out_mute(s_spk_codec, mute);
    }

    ESP_LOGI(TAG, "Player %s", mute ? "muted" : "unmuted");
    return ESP_OK;
}

bool audio_player_is_muted(void)
{
    return s_muted;
}

esp_err_t audio_player_clear_buffer(void)
{
    if (!s_initialized || s_ring_buffer == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    // Drain the ring buffer
    size_t received_size;
    void *data;
    while ((data = xRingbufferReceiveUpTo(s_ring_buffer, &received_size, 0, 4096)) != NULL) {
        vRingbufferReturnItem(s_ring_buffer, data);
    }

    return ESP_OK;
}

uint8_t audio_player_get_buffer_level(void)
{
    if (!s_initialized || s_ring_buffer == NULL) {
        return 0;
    }

    size_t free_size = xRingbufferGetCurFreeSize(s_ring_buffer);
    size_t used = PLAYER_RING_BUFFER_SIZE - free_size;
    return (uint8_t)(used * 100 / PLAYER_RING_BUFFER_SIZE);
}

esp_err_t audio_player_set_callback(audio_player_callback_t callback, void *user_data)
{
    s_callback = callback;
    s_callback_user_data = user_data;
    return ESP_OK;
}

esp_err_t audio_player_wait_finish(uint32_t timeout_ms)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    TickType_t ticks = (timeout_ms == 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);

    if (xSemaphoreTake(s_finish_sem, ticks) == pdTRUE) {
        return ESP_OK;
    }

    return ESP_ERR_TIMEOUT;
}

esp_err_t audio_player_set_format(audio_format_t format)
{
    s_config.format = format;
    return ESP_OK;
}

esp_err_t audio_player_decode_and_play(const uint8_t *data, size_t size, audio_format_t format)
{
    if (!s_initialized || data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    switch (format) {
        case AUDIO_FORMAT_PCM:
            // Direct playback
            return audio_player_write_blocking(data, size);

        case AUDIO_FORMAT_WAV:
            // Skip WAV header (44 bytes typically)
            if (size > 44) {
                return audio_player_write_blocking(data + 44, size - 44);
            }
            break;

        case AUDIO_FORMAT_MP3:
        case AUDIO_FORMAT_OPUS:
            // TODO: Implement decoder using ESP-ADF audio decoders
            ESP_LOGW(TAG, "MP3/Opus decoding not implemented");
            return ESP_ERR_NOT_SUPPORTED;

        default:
            return ESP_ERR_INVALID_ARG;
    }

    return ESP_OK;
}
