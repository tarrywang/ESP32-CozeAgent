/**
 * @file audio_recorder.h
 * @brief Audio recorder with AEC, NS, and VAD processing
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "audio_pipeline.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================
// Recorder Configuration
// ============================================

/**
 * @brief Recorder processing configuration
 */
typedef struct {
    bool enable_aec;            // Acoustic Echo Cancellation
    bool enable_ns;             // Noise Suppression
    bool enable_vad;            // Voice Activity Detection
    int vad_mode;               // VAD aggressiveness (0-3, 3=most aggressive)
    int ns_level;               // NS level (0-3, 3=highest suppression)
    int aec_mode;               // AEC mode (0-2)
} audio_recorder_config_t;

// Default recorder configuration
#define AUDIO_RECORDER_DEFAULT_CONFIG() { \
    .enable_aec = true,                   \
    .enable_ns = true,                    \
    .enable_vad = true,                   \
    .vad_mode = 2,                        \
    .ns_level = 2,                        \
    .aec_mode = 1,                        \
}

// ============================================
// Recorder Function Declarations
// ============================================

/**
 * @brief Initialize audio recorder
 *
 * @param config Recorder configuration (NULL for defaults)
 * @return ESP_OK on success
 */
esp_err_t audio_recorder_init(const audio_recorder_config_t *config);

/**
 * @brief Deinitialize audio recorder
 *
 * @return ESP_OK on success
 */
esp_err_t audio_recorder_deinit(void);

/**
 * @brief Start recording
 *
 * @return ESP_OK on success
 */
esp_err_t audio_recorder_start(void);

/**
 * @brief Stop recording
 *
 * @return ESP_OK on success
 */
esp_err_t audio_recorder_stop(void);

/**
 * @brief Check if recording is active
 *
 * @return true if recording
 */
bool audio_recorder_is_running(void);

/**
 * @brief Read processed audio data
 *
 * @param buffer Output buffer
 * @param size Buffer size
 * @param bytes_read Actual bytes read
 * @param timeout_ms Timeout
 * @return ESP_OK on success
 */
esp_err_t audio_recorder_read(uint8_t *buffer, size_t size,
                               size_t *bytes_read, uint32_t timeout_ms);

/**
 * @brief Get current VAD state
 *
 * @return VAD state
 */
vad_state_t audio_recorder_get_vad_state(void);

/**
 * @brief Feed reference signal for AEC
 *
 * Call this with playback audio to enable AEC.
 *
 * @param data Reference audio data
 * @param size Size in bytes
 * @return ESP_OK on success
 */
esp_err_t audio_recorder_feed_aec_ref(const uint8_t *data, size_t size);

/**
 * @brief Set VAD callback
 *
 * @param callback Callback function
 * @param user_data User context
 * @return ESP_OK on success
 */
esp_err_t audio_recorder_set_vad_callback(
    void (*callback)(vad_state_t state, void *user_data),
    void *user_data);

/**
 * @brief Configure VAD parameters
 *
 * @param mode VAD aggressiveness (0-3)
 * @param silence_ms Silence duration to trigger VOICE_END
 * @return ESP_OK on success
 */
esp_err_t audio_recorder_configure_vad(int mode, uint32_t silence_ms);

/**
 * @brief Get audio level (RMS)
 *
 * @return Audio level (0-100)
 */
uint8_t audio_recorder_get_level(void);

#ifdef __cplusplus
}
#endif
