/**
 * @file bsp_display.h
 * @brief AMOLED Display driver interface for SH8601 controller
 *
 * Provides display initialization, LVGL integration, and backlight control
 * for the 466x466 AMOLED display.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "esp_lcd_types.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================
// Display Configuration
// ============================================

/**
 * @brief Display configuration structure
 */
typedef struct {
    uint16_t width;             // Display width in pixels
    uint16_t height;            // Display height in pixels
    uint8_t rotation;           // Rotation (0, 90, 180, 270)
    bool mirror_x;              // Mirror X axis
    bool mirror_y;              // Mirror Y axis
    bool swap_xy;               // Swap X and Y axes
    uint8_t brightness;         // Brightness level (0-100)
} bsp_display_config_t;

// Default display configuration
#define BSP_DISPLAY_DEFAULT_CONFIG() { \
    .width = 466,                      \
    .height = 466,                     \
    .rotation = 0,                     \
    .mirror_x = false,                 \
    .mirror_y = false,                 \
    .swap_xy = false,                  \
    .brightness = 100,                 \
}

// ============================================
// Display Function Declarations
// ============================================

/**
 * @brief Initialize the AMOLED display
 *
 * Initializes SPI bus, LCD panel driver, and LVGL display driver.
 *
 * @param config Display configuration (NULL for defaults)
 * @return ESP_OK on success
 */
esp_err_t bsp_display_init(const bsp_display_config_t *config);

/**
 * @brief Deinitialize display
 *
 * @return ESP_OK on success
 */
esp_err_t bsp_display_deinit(void);

/**
 * @brief Get LVGL display object
 *
 * @return Pointer to LVGL display, or NULL if not initialized
 */
lv_disp_t *bsp_display_get_lv_disp(void);

/**
 * @brief Set display brightness
 *
 * @param brightness Brightness level (0-100)
 * @return ESP_OK on success
 */
esp_err_t bsp_display_set_brightness(uint8_t brightness);

/**
 * @brief Get current display brightness
 *
 * @return Current brightness level (0-100)
 */
uint8_t bsp_display_get_brightness(void);

/**
 * @brief Turn display on/off
 *
 * @param on true to turn on, false to turn off
 * @return ESP_OK on success
 */
esp_err_t bsp_display_power(bool on);

/**
 * @brief Set display rotation
 *
 * @param rotation Rotation angle (0, 90, 180, 270)
 * @return ESP_OK on success
 */
esp_err_t bsp_display_set_rotation(uint16_t rotation);

/**
 * @brief Lock LVGL mutex for thread-safe operations
 *
 * @param timeout_ms Timeout in milliseconds (-1 for indefinite)
 * @return true if lock acquired, false on timeout
 */
bool bsp_display_lock(int timeout_ms);

/**
 * @brief Unlock LVGL mutex
 */
void bsp_display_unlock(void);

/**
 * @brief Force display refresh
 *
 * Triggers an immediate display refresh. Use sparingly.
 */
void bsp_display_refresh(void);

/**
 * @brief Start LVGL tick timer
 *
 * @return ESP_OK on success
 */
esp_err_t bsp_display_start_tick_timer(void);

/**
 * @brief Stop LVGL tick timer
 *
 * @return ESP_OK on success
 */
esp_err_t bsp_display_stop_tick_timer(void);

/**
 * @brief Get display panel handle for advanced operations
 *
 * @return LCD panel handle, or NULL if not initialized
 */
esp_lcd_panel_handle_t bsp_display_get_panel_handle(void);

/**
 * @brief Fill display with solid color
 *
 * Useful for testing and boot screen.
 *
 * @param color Color in RGB565 format
 */
void bsp_display_fill_color(uint16_t color);

/**
 * @brief Draw bitmap to display at specified position
 *
 * @param x X position
 * @param y Y position
 * @param w Width
 * @param h Height
 * @param data Bitmap data (RGB565)
 */
void bsp_display_draw_bitmap(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                             const uint16_t *data);

#ifdef __cplusplus
}
#endif
