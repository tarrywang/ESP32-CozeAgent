/**
 * @file audio_player.h
 * @brief Audio player for TTS playback
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================
// Player Configuration
// ============================================

/**
 * @brief Player state
 */
typedef enum {
    AUDIO_PLAYER_STATE_IDLE = 0,
    AUDIO_PLAYER_STATE_PLAYING,
    AUDIO_PLAYER_STATE_PAUSED,
    AUDIO_PLAYER_STATE_FINISHED,
} audio_player_state_t;

/**
 * @brief Audio format
 */
typedef enum {
    AUDIO_FORMAT_PCM = 0,       // Raw PCM
    AUDIO_FORMAT_WAV,           // WAV container
    AUDIO_FORMAT_MP3,           // MP3 encoded
    AUDIO_FORMAT_OPUS,          // Opus encoded
} audio_format_t;

/**
 * @brief Player configuration
 */
typedef struct {
    uint32_t sample_rate;       // Sample rate
    uint8_t bits_per_sample;    // Bits per sample
    uint8_t channels;           // Number of channels
    uint8_t volume;             // Initial volume (0-100)
    audio_format_t format;      // Audio format
} audio_player_config_t;

// Default player configuration
#define AUDIO_PLAYER_DEFAULT_CONFIG() { \
    .sample_rate = 16000,               \
    .bits_per_sample = 16,              \
    .channels = 1,                      \
    .volume = 80,                       \
    .format = AUDIO_FORMAT_PCM,         \
}

/**
 * @brief Playback finished callback
 */
typedef void (*audio_player_callback_t)(audio_player_state_t state, void *user_data);

// ============================================
// Player Function Declarations
// ============================================

/**
 * @brief Initialize audio player
 *
 * @param config Player configuration (NULL for defaults)
 * @return ESP_OK on success
 */
esp_err_t audio_player_init(const audio_player_config_t *config);

/**
 * @brief Deinitialize audio player
 *
 * @return ESP_OK on success
 */
esp_err_t audio_player_deinit(void);

/**
 * @brief Start playback
 *
 * @return ESP_OK on success
 */
esp_err_t audio_player_start(void);

/**
 * @brief Stop playback
 *
 * @return ESP_OK on success
 */
esp_err_t audio_player_stop(void);

/**
 * @brief Pause playback
 *
 * @return ESP_OK on success
 */
esp_err_t audio_player_pause(void);

/**
 * @brief Resume playback
 *
 * @return ESP_OK on success
 */
esp_err_t audio_player_resume(void);

/**
 * @brief Check if playing
 *
 * @return true if playing
 */
bool audio_player_is_playing(void);

/**
 * @brief Get player state
 *
 * @return Current state
 */
audio_player_state_t audio_player_get_state(void);

/**
 * @brief Write audio data to player
 *
 * @param data Audio data
 * @param size Size in bytes
 * @param timeout_ms Timeout
 * @return Bytes written, or -1 on error
 */
int audio_player_write(const uint8_t *data, size_t size, uint32_t timeout_ms);

/**
 * @brief Write audio data and block until played
 *
 * @param data Audio data
 * @param size Size in bytes
 * @return ESP_OK on success
 */
esp_err_t audio_player_write_blocking(const uint8_t *data, size_t size);

/**
 * @brief Set volume
 *
 * @param volume Volume (0-100)
 * @return ESP_OK on success
 */
esp_err_t audio_player_set_volume(uint8_t volume);

/**
 * @brief Get volume
 *
 * @return Current volume (0-100)
 */
uint8_t audio_player_get_volume(void);

/**
 * @brief Mute/unmute
 *
 * @param mute true to mute
 * @return ESP_OK on success
 */
esp_err_t audio_player_set_mute(bool mute);

/**
 * @brief Check if muted
 *
 * @return true if muted
 */
bool audio_player_is_muted(void);

/**
 * @brief Clear playback buffer
 *
 * @return ESP_OK on success
 */
esp_err_t audio_player_clear_buffer(void);

/**
 * @brief Get buffer fill level
 *
 * @return Fill level percentage (0-100)
 */
uint8_t audio_player_get_buffer_level(void);

/**
 * @brief Set playback finished callback
 *
 * @param callback Callback function
 * @param user_data User context
 * @return ESP_OK on success
 */
esp_err_t audio_player_set_callback(audio_player_callback_t callback, void *user_data);

/**
 * @brief Wait for playback to finish
 *
 * @param timeout_ms Timeout (0 for indefinite)
 * @return ESP_OK on finish, ESP_ERR_TIMEOUT on timeout
 */
esp_err_t audio_player_wait_finish(uint32_t timeout_ms);

/**
 * @brief Set audio format
 *
 * @param format Audio format
 * @return ESP_OK on success
 */
esp_err_t audio_player_set_format(audio_format_t format);

/**
 * @brief Decode and play audio from buffer
 *
 * Supports multiple formats based on configuration.
 *
 * @param data Encoded audio data
 * @param size Size in bytes
 * @param format Audio format
 * @return ESP_OK on success
 */
esp_err_t audio_player_decode_and_play(const uint8_t *data, size_t size, audio_format_t format);

#ifdef __cplusplus
}
#endif
