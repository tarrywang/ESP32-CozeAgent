/**
 * @file bsp_i2s.h
 * @brief I2S driver interface for microphone array and speaker
 *
 * Handles dual-channel I2S:
 * - I2S0: Microphone input (dual-mic array)
 * - I2S1: Speaker output (MAX98357A or equivalent)
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "driver/i2s_std.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================
// I2S Configuration
// ============================================

/**
 * @brief I2S microphone configuration structure
 */
typedef struct {
    uint32_t sample_rate;       // Sample rate in Hz (default: 16000)
    uint8_t bits_per_sample;    // Bits per sample (default: 16)
    uint8_t channels;           // Number of channels (1=mono, 2=stereo)
    size_t buffer_size;         // DMA buffer size in bytes
    size_t buffer_count;        // Number of DMA buffers
} bsp_i2s_mic_config_t;

/**
 * @brief I2S speaker configuration structure
 */
typedef struct {
    uint32_t sample_rate;       // Sample rate in Hz (default: 16000)
    uint8_t bits_per_sample;    // Bits per sample (default: 16)
    uint8_t channels;           // Number of channels (1=mono, 2=stereo)
    size_t buffer_size;         // DMA buffer size in bytes
    size_t buffer_count;        // Number of DMA buffers
    uint8_t volume;             // Volume level (0-100)
} bsp_i2s_spk_config_t;

// ============================================
// Default Configurations
// ============================================

#define BSP_I2S_MIC_DEFAULT_CONFIG() { \
    .sample_rate = 16000,              \
    .bits_per_sample = 16,             \
    .channels = 1,                     \
    .buffer_size = 1024,               \
    .buffer_count = 4,                 \
}

#define BSP_I2S_SPK_DEFAULT_CONFIG() { \
    .sample_rate = 16000,              \
    .bits_per_sample = 16,             \
    .channels = 1,                     \
    .buffer_size = 1024,               \
    .buffer_count = 4,                 \
    .volume = 80,                      \
}

// ============================================
// I2S Function Declarations
// ============================================

/**
 * @brief Initialize I2S microphone driver
 *
 * Configures I2S0 for receiving audio from dual-mic array.
 * The two microphones are mixed into mono output.
 *
 * @param config Microphone configuration (NULL for defaults)
 * @return ESP_OK on success
 */
esp_err_t bsp_i2s_mic_init(const bsp_i2s_mic_config_t *config);

/**
 * @brief Initialize I2S speaker driver
 *
 * Configures I2S1 for sending audio to speaker amplifier.
 *
 * @param config Speaker configuration (NULL for defaults)
 * @return ESP_OK on success
 */
esp_err_t bsp_i2s_spk_init(const bsp_i2s_spk_config_t *config);

/**
 * @brief Deinitialize I2S microphone driver
 *
 * @return ESP_OK on success
 */
esp_err_t bsp_i2s_mic_deinit(void);

/**
 * @brief Deinitialize I2S speaker driver
 *
 * @return ESP_OK on success
 */
esp_err_t bsp_i2s_spk_deinit(void);

/**
 * @brief Read audio data from microphone
 *
 * Reads audio samples from the microphone DMA buffer.
 * This function blocks until data is available or timeout.
 *
 * @param buffer Buffer to store audio data
 * @param buffer_size Size of buffer in bytes
 * @param bytes_read Pointer to store actual bytes read
 * @param timeout_ms Timeout in milliseconds
 * @return ESP_OK on success, ESP_ERR_TIMEOUT on timeout
 */
esp_err_t bsp_i2s_mic_read(void *buffer, size_t buffer_size,
                           size_t *bytes_read, uint32_t timeout_ms);

/**
 * @brief Write audio data to speaker
 *
 * Writes audio samples to the speaker DMA buffer.
 * This function blocks until data is written or timeout.
 *
 * @param buffer Buffer containing audio data
 * @param buffer_size Size of data in bytes
 * @param bytes_written Pointer to store actual bytes written
 * @param timeout_ms Timeout in milliseconds
 * @return ESP_OK on success, ESP_ERR_TIMEOUT on timeout
 */
esp_err_t bsp_i2s_spk_write(const void *buffer, size_t buffer_size,
                            size_t *bytes_written, uint32_t timeout_ms);

/**
 * @brief Start microphone recording
 *
 * @return ESP_OK on success
 */
esp_err_t bsp_i2s_mic_start(void);

/**
 * @brief Stop microphone recording
 *
 * @return ESP_OK on success
 */
esp_err_t bsp_i2s_mic_stop(void);

/**
 * @brief Start speaker playback
 *
 * @return ESP_OK on success
 */
esp_err_t bsp_i2s_spk_start(void);

/**
 * @brief Stop speaker playback
 *
 * @return ESP_OK on success
 */
esp_err_t bsp_i2s_spk_stop(void);

/**
 * @brief Set speaker volume
 *
 * @param volume Volume level (0-100)
 * @return ESP_OK on success
 */
esp_err_t bsp_i2s_spk_set_volume(uint8_t volume);

/**
 * @brief Get speaker volume
 *
 * @return Current volume level (0-100)
 */
uint8_t bsp_i2s_spk_get_volume(void);

/**
 * @brief Mute speaker output
 *
 * @param mute true to mute, false to unmute
 * @return ESP_OK on success
 */
esp_err_t bsp_i2s_spk_mute(bool mute);

/**
 * @brief Check if speaker is muted
 *
 * @return true if muted
 */
bool bsp_i2s_spk_is_muted(void);

/**
 * @brief Clear speaker buffer (flush pending audio)
 *
 * @return ESP_OK on success
 */
esp_err_t bsp_i2s_spk_clear_buffer(void);

/**
 * @brief Get I2S microphone handle for advanced operations
 *
 * @return I2S channel handle, or NULL if not initialized
 */
i2s_chan_handle_t bsp_i2s_mic_get_handle(void);

/**
 * @brief Get I2S speaker handle for advanced operations
 *
 * @return I2S channel handle, or NULL if not initialized
 */
i2s_chan_handle_t bsp_i2s_spk_get_handle(void);

#ifdef __cplusplus
}
#endif
