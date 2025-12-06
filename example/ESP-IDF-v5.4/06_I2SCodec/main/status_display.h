/*
 * Status Display Header
 * System status pages with touch switching
 */

#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Status page types
 */
typedef enum {
    STATUS_PAGE_SYSTEM,     // System info: heap, CPU, uptime
    STATUS_PAGE_AUDIO,      // Audio status: sample rate, volume, mode
    STATUS_PAGE_WIFI,       // WiFi status (placeholder)
    STATUS_PAGE_MAX
} status_page_t;

/**
 * @brief Audio status info structure
 */
typedef struct {
    int sample_rate;
    int volume;
    bool is_playing;
    bool is_recording;
    const char *mode;       // "Music" or "Echo"
} audio_status_t;

/**
 * @brief Initialize display and LVGL
 * @return ESP_OK on success
 */
esp_err_t status_display_init(void);

/**
 * @brief Switch to specific status page
 * @param page Page to switch to
 */
void status_display_switch_page(status_page_t page);

/**
 * @brief Switch to next page
 */
void status_display_next_page(void);

/**
 * @brief Update system status display
 * Called periodically to refresh data
 */
void status_display_update(void);

/**
 * @brief Update audio status
 * @param status Pointer to audio status info
 */
void status_display_set_audio_status(const audio_status_t *status);

/**
 * @brief Lock LVGL mutex for thread-safe access
 * @param timeout_ms Timeout in ms (-1 for infinite)
 * @return true if locked
 */
bool status_display_lock(int timeout_ms);

/**
 * @brief Unlock LVGL mutex
 */
void status_display_unlock(void);

#ifdef __cplusplus
}
#endif
