/**
 * @file audio_pipeline.h
 * @brief Audio Pipeline manager for voice recording and playback
 *
 * Provides a unified interface for managing audio recording and playback
 * pipelines with AEC, NS, and VAD processing.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================
// Audio Pipeline Configuration
// ============================================

#define AUDIO_SAMPLE_RATE       8000    // 8kHz for G.711 (narrowband voice)
#define AUDIO_BITS_PER_SAMPLE   16      // 16-bit PCM samples (before G.711 encoding)
#define AUDIO_CHANNELS          1       // Mono
#define AUDIO_FRAME_MS          60      // 60ms frame size
#define AUDIO_FRAME_SAMPLES     (AUDIO_SAMPLE_RATE * AUDIO_FRAME_MS / 1000)  // 480 samples
#define AUDIO_FRAME_BYTES       (AUDIO_FRAME_SAMPLES * AUDIO_BITS_PER_SAMPLE / 8)  // 960 bytes PCM16

/**
 * @brief Audio pipeline state
 */
typedef enum {
    AUDIO_PIPELINE_STATE_IDLE = 0,      // Not running
    AUDIO_PIPELINE_STATE_RECORDING,      // Recording active
    AUDIO_PIPELINE_STATE_PLAYING,        // Playback active
    AUDIO_PIPELINE_STATE_FULL_DUPLEX,   // Both recording and playback
} audio_pipeline_state_t;

/**
 * @brief VAD (Voice Activity Detection) state
 */
typedef enum {
    VAD_STATE_SILENCE = 0,      // No voice detected
    VAD_STATE_VOICE_START,      // Voice just started
    VAD_STATE_VOICE,            // Voice ongoing
    VAD_STATE_VOICE_END,        // Voice just ended
} vad_state_t;

/**
 * @brief Audio data callback function type
 *
 * Called when audio data is available from recording or ready to play.
 *
 * @param data Audio data buffer
 * @param size Size of data in bytes
 * @param vad_state Current VAD state
 * @param user_data User context pointer
 */
typedef void (*audio_data_callback_t)(const uint8_t *data, size_t size,
                                       vad_state_t vad_state, void *user_data);

/**
 * @brief Audio event callback function type
 *
 * Called for pipeline state change events.
 *
 * @param event Event type
 * @param user_data User context pointer
 */
typedef void (*audio_event_callback_t)(audio_pipeline_state_t event, void *user_data);

/**
 * @brief Audio pipeline configuration
 */
typedef struct {
    uint32_t sample_rate;               // Sample rate in Hz
    uint8_t bits_per_sample;            // Bits per sample
    uint8_t channels;                   // Number of channels
    bool enable_aec;                    // Enable Acoustic Echo Cancellation
    bool enable_ns;                     // Enable Noise Suppression
    bool enable_vad;                    // Enable Voice Activity Detection
    audio_data_callback_t record_cb;    // Recording data callback
    audio_event_callback_t event_cb;    // Event callback
    void *user_data;                    // User context for callbacks
} audio_pipeline_config_t;

// Default audio pipeline configuration
#define AUDIO_PIPELINE_DEFAULT_CONFIG() {  \
    .sample_rate = AUDIO_SAMPLE_RATE,      \
    .bits_per_sample = AUDIO_BITS_PER_SAMPLE, \
    .channels = AUDIO_CHANNELS,            \
    .enable_aec = true,                    \
    .enable_ns = true,                     \
    .enable_vad = true,                    \
    .record_cb = NULL,                     \
    .event_cb = NULL,                      \
    .user_data = NULL,                     \
}

// ============================================
// Audio Pipeline Function Declarations
// ============================================

/**
 * @brief Initialize audio pipeline
 *
 * Sets up audio recording and playback pipelines with processing.
 *
 * @return ESP_OK on success
 */
esp_err_t audio_pipeline_init(void);

/**
 * @brief Deinitialize audio pipeline
 *
 * @return ESP_OK on success
 */
esp_err_t audio_pipeline_deinit(void);

/**
 * @brief Configure audio pipeline
 *
 * @param config Pipeline configuration
 * @return ESP_OK on success
 */
esp_err_t audio_pipeline_configure(const audio_pipeline_config_t *config);

/**
 * @brief Start audio recording
 *
 * Begins capturing audio from microphone with processing.
 *
 * @return ESP_OK on success
 */
esp_err_t audio_pipeline_start_recording(void);

/**
 * @brief Stop audio recording
 *
 * @return ESP_OK on success
 */
esp_err_t audio_pipeline_stop_recording(void);

/**
 * @brief Start audio playback
 *
 * @return ESP_OK on success
 */
esp_err_t audio_pipeline_start_playback(void);

/**
 * @brief Stop audio playback
 *
 * @return ESP_OK on success
 */
esp_err_t audio_pipeline_stop_playback(void);

/**
 * @brief Write audio data to playback buffer
 *
 * @param data Audio data (PCM)
 * @param size Size in bytes
 * @param timeout_ms Timeout in milliseconds
 * @return Number of bytes written, or -1 on error
 */
int audio_pipeline_write(const uint8_t *data, size_t size, uint32_t timeout_ms);

/**
 * @brief Read audio data from recording buffer
 *
 * @param data Buffer to store audio data
 * @param size Maximum size to read
 * @param timeout_ms Timeout in milliseconds
 * @return Number of bytes read, or -1 on error
 */
int audio_pipeline_read(uint8_t *data, size_t size, uint32_t timeout_ms);

/**
 * @brief Get current pipeline state
 *
 * @return Current pipeline state
 */
audio_pipeline_state_t audio_pipeline_get_state(void);

/**
 * @brief Get current VAD state
 *
 * @return Current VAD state
 */
vad_state_t audio_pipeline_get_vad_state(void);

/**
 * @brief Check if voice is currently detected
 *
 * @return true if voice is detected
 */
bool audio_pipeline_is_voice_active(void);

/**
 * @brief Set playback volume
 *
 * @param volume Volume level (0-100)
 * @return ESP_OK on success
 */
esp_err_t audio_pipeline_set_volume(uint8_t volume);

/**
 * @brief Get playback volume
 *
 * @return Current volume level (0-100)
 */
uint8_t audio_pipeline_get_volume(void);

/**
 * @brief Mute/unmute audio
 *
 * @param mute true to mute, false to unmute
 * @return ESP_OK on success
 */
esp_err_t audio_pipeline_set_mute(bool mute);

/**
 * @brief Clear playback buffer
 *
 * @return ESP_OK on success
 */
esp_err_t audio_pipeline_clear_playback_buffer(void);

/**
 * @brief Start pipeline tasks
 *
 * Creates and starts recording and playback tasks.
 *
 * @return ESP_OK on success
 */
esp_err_t audio_pipeline_start_tasks(void);

/**
 * @brief Stop pipeline tasks
 *
 * @return ESP_OK on success
 */
esp_err_t audio_pipeline_stop_tasks(void);

/**
 * @brief Get recording queue handle for direct access
 *
 * @return Queue handle, or NULL if not initialized
 */
QueueHandle_t audio_pipeline_get_record_queue(void);

/**
 * @brief Get playback queue handle for direct access
 *
 * @return Queue handle, or NULL if not initialized
 */
QueueHandle_t audio_pipeline_get_playback_queue(void);

/**
 * @brief Enable/disable AEC
 *
 * @param enable true to enable, false to disable
 * @return ESP_OK on success
 */
esp_err_t audio_pipeline_enable_aec(bool enable);

/**
 * @brief Enable/disable noise suppression
 *
 * @param enable true to enable, false to disable
 * @return ESP_OK on success
 */
esp_err_t audio_pipeline_enable_ns(bool enable);

/**
 * @brief Enable/disable VAD
 *
 * @param enable true to enable, false to disable
 * @return ESP_OK on success
 */
esp_err_t audio_pipeline_enable_vad(bool enable);

#ifdef __cplusplus
}
#endif
