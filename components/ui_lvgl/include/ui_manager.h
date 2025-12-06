/**
 * @file ui_manager.h
 * @brief UI Manager for LVGL-based voice assistant interface
 *
 * Manages UI pages, animations, and state transitions for the
 * Coze Voice Agent display.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================
// UI Configuration
// ============================================

// Display dimensions (466x466 circular AMOLED)
#define UI_SCREEN_WIDTH         466
#define UI_SCREEN_HEIGHT        466

// Colors (dark theme optimized for AMOLED)
#define UI_COLOR_BG             lv_color_hex(0x000000)  // Pure black
#define UI_COLOR_PRIMARY        lv_color_hex(0x4A90D9)  // Blue accent
#define UI_COLOR_SECONDARY      lv_color_hex(0x2ECC71)  // Green
#define UI_COLOR_ACCENT         lv_color_hex(0x9B59B6)  // Purple
#define UI_COLOR_TEXT           lv_color_hex(0xFFFFFF)  // White text
#define UI_COLOR_TEXT_DIM       lv_color_hex(0x888888)  // Dimmed text
#define UI_COLOR_ERROR          lv_color_hex(0xE74C3C)  // Red error
#define UI_COLOR_LISTENING      lv_color_hex(0x3498DB)  // Blue listening
#define UI_COLOR_THINKING       lv_color_hex(0xF39C12)  // Orange thinking
#define UI_COLOR_SPEAKING       lv_color_hex(0x2ECC71)  // Green speaking

// Animation timings
#define UI_ANIM_DURATION_FAST   200     // Fast transitions
#define UI_ANIM_DURATION_NORMAL 400     // Normal transitions
#define UI_ANIM_DURATION_SLOW   800     // Slow transitions

// ============================================
// UI Pages
// ============================================

/**
 * @brief UI page identifiers
 */
typedef enum {
    UI_PAGE_BOOT = 0,       // Boot/splash screen
    UI_PAGE_IDLE,           // Idle state - ready for interaction
    UI_PAGE_LISTENING,      // Listening for user speech
    UI_PAGE_THINKING,       // Processing/thinking
    UI_PAGE_SPEAKING,       // Playing AI response
    UI_PAGE_ERROR,          // Error state
    UI_PAGE_SETTINGS,       // Settings page
    UI_PAGE_MAX
} ui_page_t;

/**
 * @brief UI event types
 */
typedef enum {
    UI_EVENT_NONE = 0,
    UI_EVENT_TAP,           // Screen tap
    UI_EVENT_DOUBLE_TAP,    // Double tap
    UI_EVENT_LONG_PRESS,    // Long press
    UI_EVENT_SWIPE_UP,      // Swipe gestures
    UI_EVENT_SWIPE_DOWN,
    UI_EVENT_SWIPE_LEFT,
    UI_EVENT_SWIPE_RIGHT,
    UI_EVENT_BUTTON,        // Physical button press
} ui_event_t;

/**
 * @brief UI event callback function type
 */
typedef void (*ui_event_callback_t)(ui_event_t event, void *user_data);

// ============================================
// UI Manager Function Declarations
// ============================================

/**
 * @brief Initialize UI manager
 *
 * Sets up LVGL display, creates all UI pages, and starts UI task.
 *
 * @return ESP_OK on success
 */
esp_err_t ui_manager_init(void);

/**
 * @brief Deinitialize UI manager
 *
 * @return ESP_OK on success
 */
esp_err_t ui_manager_deinit(void);

/**
 * @brief Start UI task
 *
 * Creates and starts the LVGL handler task.
 *
 * @return ESP_OK on success
 */
esp_err_t ui_manager_start_task(void);

/**
 * @brief Stop UI task
 *
 * @return ESP_OK on success
 */
esp_err_t ui_manager_stop_task(void);

/**
 * @brief Set current UI page
 *
 * Transitions to the specified page with animation.
 *
 * @param page Page to display
 * @return ESP_OK on success
 */
esp_err_t ui_manager_set_page(ui_page_t page);

/**
 * @brief Get current UI page
 *
 * @return Current page identifier
 */
ui_page_t ui_manager_get_page(void);

/**
 * @brief Show boot screen
 */
void ui_manager_show_boot_screen(void);

/**
 * @brief Show status message
 *
 * @param message Status message
 * @param success true for success indicator, false for failure
 */
void ui_manager_show_status(const char *message, bool success);

/**
 * @brief Show error message
 *
 * @param message Error message
 */
void ui_manager_show_error(const char *message);

/**
 * @brief Update transcript text (during listening/speaking)
 *
 * @param text Transcript text
 * @param is_user true if user speech, false if AI response
 */
void ui_manager_update_transcript(const char *text, bool is_user);

/**
 * @brief Clear transcript
 */
void ui_manager_clear_transcript(void);

/**
 * @brief Update audio level indicator
 *
 * @param level Audio level (0-100)
 */
void ui_manager_update_audio_level(uint8_t level);

/**
 * @brief Show/hide WiFi indicator
 *
 * @param connected true if WiFi connected
 * @param rssi Signal strength (optional, -1 to ignore)
 */
void ui_manager_update_wifi_status(bool connected, int rssi);

/**
 * @brief Update battery indicator
 *
 * @param level Battery level (0-100)
 * @param charging true if charging
 */
void ui_manager_update_battery(uint8_t level, bool charging);

/**
 * @brief Register UI event callback
 *
 * @param callback Callback function
 * @param user_data User context
 * @return ESP_OK on success
 */
esp_err_t ui_manager_register_callback(ui_event_callback_t callback, void *user_data);

/**
 * @brief Lock UI mutex for thread-safe LVGL operations
 *
 * @param timeout_ms Timeout in milliseconds
 * @return true if lock acquired
 */
bool ui_manager_lock(int timeout_ms);

/**
 * @brief Unlock UI mutex
 */
void ui_manager_unlock(void);

/**
 * @brief Force UI refresh
 */
void ui_manager_refresh(void);

/**
 * @brief Get page name string
 *
 * @param page Page identifier
 * @return Page name string
 */
const char *ui_manager_page_to_string(ui_page_t page);

#ifdef __cplusplus
}
#endif
