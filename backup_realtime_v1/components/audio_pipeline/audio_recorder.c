/**
 * @file audio_recorder.c
 * @brief Audio recorder implementation with AEC, NS, and VAD
 *
 * Uses ESP codec device API from official Waveshare BSP.
 */

#include "audio_recorder.h"

// Use official Waveshare BSP codec dev API
#include "esp_codec_dev.h"

#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/ringbuf.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

static const char *TAG = "AUDIO_RECORDER";

// ============================================
// External declarations from app_main
// ============================================
extern esp_codec_dev_handle_t app_get_mic_codec(void);

// ============================================
// Configuration
// ============================================

#define RECORDER_RING_BUFFER_SIZE   (AUDIO_FRAME_BYTES * 50)  // 1000ms buffer (increased from 30)
#define VAD_SILENCE_THRESHOLD_MS    500     // 500ms silence to end speech
#define VAD_ENERGY_THRESHOLD        100     // Energy threshold for voice detection

// ============================================
// Private Variables
// ============================================

static bool s_initialized = false;
static bool s_running = false;
static audio_recorder_config_t s_config;

// Ring buffer for processed audio
static RingbufHandle_t s_ring_buffer = NULL;

// VAD state machine
static vad_state_t s_vad_state = VAD_STATE_SILENCE;
static uint32_t s_silence_start_tick = 0;
static uint32_t s_voice_start_tick = 0;

// VAD callback
static void (*s_vad_callback)(vad_state_t, void *) = NULL;
static void *s_vad_user_data = NULL;

// AEC reference buffer
static int16_t *s_aec_ref_buffer = NULL;
static size_t s_aec_ref_size = 0;
static SemaphoreHandle_t s_aec_mutex = NULL;

// Audio level
static uint8_t s_audio_level = 0;

// Task handle
static TaskHandle_t s_recorder_task = NULL;
static volatile bool s_task_running = false;

// Codec handle
static esp_codec_dev_handle_t s_mic_codec = NULL;

// ============================================
// Simple DSP Functions (fallback when ESP-AFE not available)
// ============================================

/**
 * @brief Calculate RMS energy of audio samples
 */
static uint32_t calculate_energy(const int16_t *samples, size_t count)
{
    int64_t sum = 0;
    for (size_t i = 0; i < count; i++) {
        sum += (int32_t)samples[i] * samples[i];
    }
    return (uint32_t)sqrt((double)sum / count);
}

/**
 * @brief Simple high-pass filter to remove DC offset
 */
static void apply_highpass_filter(int16_t *samples, size_t count)
{
    static int32_t prev_input = 0;
    static int32_t prev_output = 0;

    // Simple first-order high-pass filter
    // y[n] = 0.98 * (y[n-1] + x[n] - x[n-1])
    for (size_t i = 0; i < count; i++) {
        int32_t input = samples[i];
        int32_t output = (prev_output * 98 + (input - prev_input) * 100) / 100;
        samples[i] = (int16_t)output;
        prev_input = input;
        prev_output = output;
    }
}

/**
 * @brief Simple noise suppression (spectral subtraction approximation)
 */
static void apply_noise_suppression(int16_t *samples, size_t count, int level)
{
    // Simple gain reduction for low-energy signals
    int threshold = 500 + level * 200;  // Adjust based on NS level

    for (size_t i = 0; i < count; i++) {
        int16_t sample = samples[i];
        if (abs(sample) < threshold) {
            samples[i] = sample / 4;  // Reduce noise floor
        }
    }
}

/**
 * @brief Simple AEC (basic echo cancellation)
 */
static void apply_aec(int16_t *mic_samples, const int16_t *ref_samples,
                       size_t count, int mode)
{
    if (ref_samples == NULL) return;

    // Simple echo subtraction with adaptive gain
    int gain = 50 + mode * 20;  // 50-90% based on mode

    for (size_t i = 0; i < count; i++) {
        int32_t echo_estimate = (int32_t)ref_samples[i] * gain / 100;
        int32_t result = (int32_t)mic_samples[i] - echo_estimate;

        // Clip to int16 range
        if (result > 32767) result = 32767;
        if (result < -32768) result = -32768;

        mic_samples[i] = (int16_t)result;
    }
}

/**
 * @brief VAD state machine update
 */
static void update_vad_state(uint32_t energy)
{
    uint32_t now = xTaskGetTickCount();
    vad_state_t prev_state = s_vad_state;

    // Update audio level (0-100 scale)
    s_audio_level = (energy > 10000) ? 100 : (uint8_t)(energy / 100);

    bool voice_detected = (energy > VAD_ENERGY_THRESHOLD);

    switch (s_vad_state) {
        case VAD_STATE_SILENCE:
            if (voice_detected) {
                s_vad_state = VAD_STATE_VOICE_START;
                s_voice_start_tick = now;
                ESP_LOGD(TAG, "VAD: Voice start detected (energy=%lu)", energy);
            }
            break;

        case VAD_STATE_VOICE_START:
            s_vad_state = VAD_STATE_VOICE;
            break;

        case VAD_STATE_VOICE:
            if (!voice_detected) {
                if (s_silence_start_tick == 0) {
                    s_silence_start_tick = now;
                } else {
                    uint32_t silence_ms = (now - s_silence_start_tick) * portTICK_PERIOD_MS;
                    if (silence_ms >= VAD_SILENCE_THRESHOLD_MS) {
                        s_vad_state = VAD_STATE_VOICE_END;
                        ESP_LOGD(TAG, "VAD: Voice end detected");
                    }
                }
            } else {
                s_silence_start_tick = 0;  // Reset silence counter
            }
            break;

        case VAD_STATE_VOICE_END:
            s_vad_state = VAD_STATE_SILENCE;
            s_silence_start_tick = 0;
            break;
    }

    // Notify callback on state change
    if (s_vad_state != prev_state && s_vad_callback) {
        s_vad_callback(s_vad_state, s_vad_user_data);
    }
}

/**
 * @brief Recorder task - reads from mic codec, processes, puts to ring buffer
 */
static void recorder_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Recorder task started");

    // Allocate processing buffer
    int16_t *process_buffer = heap_caps_malloc(AUDIO_FRAME_BYTES, MALLOC_CAP_DMA);
    if (process_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate process buffer");
        vTaskDelete(NULL);
        return;
    }

    // Open microphone codec for recording
    // NOTE: ES7210 enables MIC1+MIC2 = 2 channels, so we must request 2 channels
    // even though we only use 1 channel of audio
    esp_codec_dev_sample_info_t fs = {
        .sample_rate = AUDIO_SAMPLE_RATE,
        .channel = 2,  // ES7210 has MIC1+MIC2 enabled = 2 channels
        .bits_per_sample = 16,
    };
    ESP_LOGI(TAG, "Opening mic codec: %d Hz, %d ch, %d bits",
             fs.sample_rate, fs.channel, fs.bits_per_sample);

    if (esp_codec_dev_open(s_mic_codec, &fs) != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Failed to open microphone codec");
        free(process_buffer);
        vTaskDelete(NULL);
        return;
    }

    // Set microphone input gain - ES7210 needs this!
    // Typical values: 0.0 (unity), 30.0 (normal mic), 40.0 (high sensitivity)
    float mic_gain_db = 36.0;  // 36 dB gain for good sensitivity
    esp_err_t gain_ret = esp_codec_dev_set_in_gain(s_mic_codec, mic_gain_db);
    if (gain_ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set mic gain: %s (continuing anyway)", esp_err_to_name(gain_ret));
    } else {
        ESP_LOGI(TAG, "🎙️ Microphone gain set to %.1f dB", mic_gain_db);
    }

    uint32_t read_count = 0;
    uint32_t last_log_tick = 0;
    uint32_t data_frames = 0;

    while (s_task_running) {
        // Read from microphone codec
        // NOTE: esp_codec_dev_read returns error code (0=success), NOT bytes read!
        int ret = esp_codec_dev_read(s_mic_codec, process_buffer, AUDIO_FRAME_BYTES);
        read_count++;

        // Check if read was successful (ret == ESP_CODEC_DEV_OK which is 0)
        bool read_success = (ret == ESP_CODEC_DEV_OK);

        // Check if buffer actually has data (first few samples non-zero)
        bool has_data = false;
        if (read_success) {
            for (int i = 0; i < 10 && i < AUDIO_FRAME_BYTES / sizeof(int16_t); i++) {
                if (process_buffer[i] != 0) {
                    has_data = true;
                    break;
                }
            }
        }

        // Log status every second
        uint32_t now = xTaskGetTickCount();
        if ((now - last_log_tick) >= pdMS_TO_TICKS(1000)) {
            ESP_LOGI(TAG, "🎙️ Recorder: reads=%lu, data_frames=%lu, ret=%d, has_data=%d, sample[0]=%d",
                     read_count, data_frames, ret, has_data, process_buffer[0]);
            last_log_tick = now;
        }

        // Process audio if read was successful
        // NOTE: We assume full AUDIO_FRAME_BYTES was read since library doesn't tell us actual count
        if (read_success) {
            data_frames++;
            size_t sample_count = AUDIO_FRAME_BYTES / sizeof(int16_t);

            // Apply high-pass filter to remove DC offset
            apply_highpass_filter(process_buffer, sample_count);

            // Apply AEC if enabled and reference available
            if (s_config.enable_aec && s_aec_ref_buffer != NULL) {
                xSemaphoreTake(s_aec_mutex, portMAX_DELAY);
                apply_aec(process_buffer, s_aec_ref_buffer, sample_count, s_config.aec_mode);
                xSemaphoreGive(s_aec_mutex);
            }

            // Apply noise suppression if enabled
            if (s_config.enable_ns) {
                apply_noise_suppression(process_buffer, sample_count, s_config.ns_level);
            }

            // Calculate energy for VAD
            uint32_t energy = calculate_energy(process_buffer, sample_count);

            // Update VAD state
            if (s_config.enable_vad) {
                update_vad_state(energy);
            }

            // Put processed audio to ring buffer
            if (xRingbufferSend(s_ring_buffer, process_buffer, AUDIO_FRAME_BYTES, 0) != pdTRUE) {
                ESP_LOGW(TAG, "Ring buffer full, dropping audio frame");
            }
        } else {
            // Read failed, wait a bit before retry
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }

    // Close microphone codec
    esp_codec_dev_close(s_mic_codec);

    free(process_buffer);
    ESP_LOGI(TAG, "Recorder task stopped");
    vTaskDelete(NULL);
}

// ============================================
// Public Functions
// ============================================

esp_err_t audio_recorder_init(const audio_recorder_config_t *config)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "Recorder already initialized");
        return ESP_OK;
    }

    // Use default config if not provided
    if (config == NULL) {
        s_config = (audio_recorder_config_t)AUDIO_RECORDER_DEFAULT_CONFIG();
    } else {
        s_config = *config;
    }

    ESP_LOGI(TAG, "Initializing audio recorder (AEC=%d, NS=%d, VAD=%d)",
             s_config.enable_aec, s_config.enable_ns, s_config.enable_vad);

    // Get microphone codec handle from app_main
    s_mic_codec = app_get_mic_codec();
    if (s_mic_codec == NULL) {
        ESP_LOGE(TAG, "Microphone codec not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    // Create ring buffer
    s_ring_buffer = xRingbufferCreate(RECORDER_RING_BUFFER_SIZE, RINGBUF_TYPE_BYTEBUF);
    if (s_ring_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to create ring buffer");
        return ESP_ERR_NO_MEM;
    }

    // Create AEC mutex and buffer
    if (s_config.enable_aec) {
        s_aec_mutex = xSemaphoreCreateMutex();
        s_aec_ref_buffer = heap_caps_malloc(AUDIO_FRAME_BYTES, MALLOC_CAP_DMA);
        if (s_aec_mutex == NULL || s_aec_ref_buffer == NULL) {
            ESP_LOGE(TAG, "Failed to allocate AEC resources");
            return ESP_ERR_NO_MEM;
        }
        memset(s_aec_ref_buffer, 0, AUDIO_FRAME_BYTES);
    }

    s_vad_state = VAD_STATE_SILENCE;
    s_initialized = true;

    ESP_LOGI(TAG, "Audio recorder initialized");
    return ESP_OK;
}

esp_err_t audio_recorder_deinit(void)
{
    if (!s_initialized) {
        return ESP_OK;
    }

    // Stop if running
    if (s_running) {
        audio_recorder_stop();
    }

    // Free resources
    if (s_ring_buffer) {
        vRingbufferDelete(s_ring_buffer);
        s_ring_buffer = NULL;
    }

    if (s_aec_mutex) {
        vSemaphoreDelete(s_aec_mutex);
        s_aec_mutex = NULL;
    }

    if (s_aec_ref_buffer) {
        free(s_aec_ref_buffer);
        s_aec_ref_buffer = NULL;
    }

    s_mic_codec = NULL;
    s_initialized = false;
    ESP_LOGI(TAG, "Audio recorder deinitialized");
    return ESP_OK;
}

esp_err_t audio_recorder_start(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (s_running) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Starting audio recorder...");

    // Reset VAD state
    s_vad_state = VAD_STATE_SILENCE;
    s_silence_start_tick = 0;

    // Start recorder task (use PSRAM for stack to save internal RAM)
    s_task_running = true;
    BaseType_t ret = xTaskCreatePinnedToCoreWithCaps(
        recorder_task,
        "audio_rec",
        6144,   // Increased from 4096 for larger 60ms audio frames
        NULL,
        18,     // High priority
        &s_recorder_task,
        1,      // Core 1
        MALLOC_CAP_SPIRAM
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create recorder task");
        s_task_running = false;
        return ESP_FAIL;
    }

    s_running = true;
    ESP_LOGI(TAG, "Audio recorder started");
    return ESP_OK;
}

esp_err_t audio_recorder_stop(void)
{
    if (!s_running) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Stopping audio recorder...");

    s_task_running = false;

    // Wait for task to finish
    vTaskDelay(pdMS_TO_TICKS(100));

    s_running = false;
    s_recorder_task = NULL;

    ESP_LOGI(TAG, "Audio recorder stopped");
    return ESP_OK;
}

bool audio_recorder_is_running(void)
{
    return s_running;
}

esp_err_t audio_recorder_read(uint8_t *buffer, size_t size,
                               size_t *bytes_read, uint32_t timeout_ms)
{
    if (!s_initialized || buffer == NULL || bytes_read == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t received_size;
    void *data = xRingbufferReceiveUpTo(s_ring_buffer, &received_size,
                                         pdMS_TO_TICKS(timeout_ms), size);

    if (data != NULL) {
        memcpy(buffer, data, received_size);
        vRingbufferReturnItem(s_ring_buffer, data);
        *bytes_read = received_size;
        return ESP_OK;
    }

    *bytes_read = 0;
    return ESP_ERR_TIMEOUT;
}

vad_state_t audio_recorder_get_vad_state(void)
{
    return s_vad_state;
}

esp_err_t audio_recorder_feed_aec_ref(const uint8_t *data, size_t size)
{
    if (!s_config.enable_aec || s_aec_ref_buffer == NULL) {
        return ESP_OK;
    }

    xSemaphoreTake(s_aec_mutex, portMAX_DELAY);

    // Copy reference data (limit to buffer size)
    size_t copy_size = (size > AUDIO_FRAME_BYTES) ? AUDIO_FRAME_BYTES : size;
    memcpy(s_aec_ref_buffer, data, copy_size);
    s_aec_ref_size = copy_size;

    xSemaphoreGive(s_aec_mutex);
    return ESP_OK;
}

esp_err_t audio_recorder_set_vad_callback(
    void (*callback)(vad_state_t state, void *user_data),
    void *user_data)
{
    s_vad_callback = callback;
    s_vad_user_data = user_data;
    return ESP_OK;
}

esp_err_t audio_recorder_configure_vad(int mode, uint32_t silence_ms)
{
    if (mode < 0 || mode > 3) {
        return ESP_ERR_INVALID_ARG;
    }

    s_config.vad_mode = mode;
    // Note: silence_ms would affect VAD_SILENCE_THRESHOLD_MS but we keep it fixed for simplicity
    (void)silence_ms;

    return ESP_OK;
}

uint8_t audio_recorder_get_level(void)
{
    return s_audio_level;
}
