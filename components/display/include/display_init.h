/**
 * @file display_init.h
 * @brief Manual display initialization for ESP32-S3-Touch-AMOLED-1.75
 *
 * Uses QSPI interface with SH8601 driver and LVGL 9.x.
 * Based on example/ESP-IDF-v5.4/05_LVGL_WITH_RAM implementation.
 */

#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================
// Hardware Configuration
// ============================================

// QSPI LCD pins
#define DISPLAY_LCD_CS          GPIO_NUM_12
#define DISPLAY_LCD_PCLK        GPIO_NUM_38
#define DISPLAY_LCD_DATA0       GPIO_NUM_4
#define DISPLAY_LCD_DATA1       GPIO_NUM_5
#define DISPLAY_LCD_DATA2       GPIO_NUM_6
#define DISPLAY_LCD_DATA3       GPIO_NUM_7
#define DISPLAY_LCD_RST         GPIO_NUM_39

// Display parameters
#define DISPLAY_H_RES           466
#define DISPLAY_V_RES           466
#define DISPLAY_SPI_HOST        SPI2_HOST

// LVGL configuration
// Reduced buffer height to free internal RAM for audio pipeline
// 466 * 30 * 2 = 27,960 bytes per buffer (RGB565)
#define DISPLAY_LVGL_BUF_HEIGHT 30  // ~30 lines per buffer
#define DISPLAY_LVGL_TICK_MS    2
#define DISPLAY_LVGL_TASK_STACK (8 * 1024)
#define DISPLAY_LVGL_TASK_PRIO  2

// ============================================
// Public Functions
// ============================================

/**
 * @brief Initialize display hardware and LVGL
 *
 * This initializes:
 * - QSPI bus for LCD
 * - SH8601 panel driver
 * - LVGL library
 * - LVGL tick timer
 * - LVGL task
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t display_init(void);

/**
 * @brief Deinitialize display
 *
 * @return ESP_OK on success
 */
esp_err_t display_deinit(void);

/**
 * @brief Get LVGL display handle
 *
 * @return LVGL display pointer, or NULL if not initialized
 */
lv_display_t *display_get_lv_disp(void);

/**
 * @brief Lock LVGL mutex for thread-safe access
 *
 * @param timeout_ms Timeout in milliseconds, -1 for infinite wait
 * @return true if lock acquired, false on timeout
 */
bool display_lock(int timeout_ms);

/**
 * @brief Unlock LVGL mutex
 */
void display_unlock(void);

/**
 * @brief Start LVGL task
 *
 * Must be called after display_init() to start the LVGL rendering task.
 *
 * @return ESP_OK on success
 */
esp_err_t display_start_task(void);

/**
 * @brief Turn display on/off
 *
 * @param on true to turn on, false to turn off
 * @return ESP_OK on success
 */
esp_err_t display_power(bool on);

#ifdef __cplusplus
}
#endif
