/**
 * @file bsp_board.h
 * @brief Board Support Package for Waveshare ESP32-S3 1.75-inch AMOLED Dev Board
 *
 * This header provides the main BSP interface for initializing and controlling
 * all hardware peripherals on the target board.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================
// Board Configuration - GPIO Pin Definitions
// Waveshare ESP32-S3 1.75-inch AMOLED
// ============================================

// I2S Microphone Array (Dual-mic, digital I2S)
#define BSP_I2S_MIC_NUM         I2S_NUM_0
#define BSP_I2S_MIC_BCLK        GPIO_NUM_41
#define BSP_I2S_MIC_WS          GPIO_NUM_42
#define BSP_I2S_MIC_DATA_IN     GPIO_NUM_2

// I2S Speaker Output (MAX98357A or equivalent)
#define BSP_I2S_SPK_NUM         I2S_NUM_1
#define BSP_I2S_SPK_BCLK        GPIO_NUM_15
#define BSP_I2S_SPK_WS          GPIO_NUM_16
#define BSP_I2S_SPK_DATA_OUT    GPIO_NUM_7

// AMOLED Display (SH8601 via SPI)
#define BSP_LCD_SPI_HOST        SPI2_HOST
#define BSP_LCD_SPI_CLK         GPIO_NUM_47
#define BSP_LCD_SPI_MOSI        GPIO_NUM_21
#define BSP_LCD_SPI_CS          GPIO_NUM_45
#define BSP_LCD_DC              GPIO_NUM_40
#define BSP_LCD_RST             GPIO_NUM_39
#define BSP_LCD_BL              GPIO_NUM_38     // Backlight control (if available)

// Display Parameters
#define BSP_LCD_WIDTH           466
#define BSP_LCD_HEIGHT          466
#define BSP_LCD_PIXEL_CLOCK_HZ  (40 * 1000 * 1000)  // 40MHz SPI clock

// Touch Controller (CST9217 via I2C)
#define BSP_TOUCH_I2C_NUM       I2C_NUM_0
#define BSP_TOUCH_I2C_SDA       GPIO_NUM_4
#define BSP_TOUCH_I2C_SCL       GPIO_NUM_5
#define BSP_TOUCH_INT           GPIO_NUM_6
#define BSP_TOUCH_RST           GPIO_NUM_NC     // Not connected, uses software reset
#define BSP_TOUCH_I2C_ADDR      0x15            // CST9217 default address

// User Buttons
#define BSP_BUTTON_BOOT         GPIO_NUM_0      // BOOT button
#define BSP_BUTTON_PWR          GPIO_NUM_NC     // Power button (if available)

// LED Indicator
#define BSP_LED_GPIO            GPIO_NUM_38

// SD Card (SPI mode)
#define BSP_SD_SPI_HOST         SPI3_HOST
#define BSP_SD_CLK              GPIO_NUM_12
#define BSP_SD_MOSI             GPIO_NUM_11
#define BSP_SD_MISO             GPIO_NUM_13
#define BSP_SD_CS               GPIO_NUM_10

// Power Management (AXP2101 via I2C)
#define BSP_PMU_I2C_ADDR        0x34            // AXP2101 default address

// ============================================
// Audio Configuration
// ============================================
#define BSP_AUDIO_SAMPLE_RATE   16000           // 16kHz for voice
#define BSP_AUDIO_BITS          16              // 16-bit samples
#define BSP_AUDIO_CHANNELS      1               // Mono (mixed from dual-mic)

// ============================================
// BSP Function Declarations
// ============================================

/**
 * @brief Initialize all board hardware
 *
 * This function initializes all board peripherals including:
 * - GPIO configuration
 * - I2C bus for touch and PMU
 * - SPI bus for display
 * - I2S for audio
 * - Display controller
 * - Touch controller
 * - Button handlers
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t bsp_board_init(void);

/**
 * @brief Deinitialize board hardware
 *
 * @return ESP_OK on success
 */
esp_err_t bsp_board_deinit(void);

/**
 * @brief Get board hardware version (if available)
 *
 * @return Hardware version string
 */
const char *bsp_board_get_version(void);

/**
 * @brief Set LED state
 *
 * @param on true to turn LED on, false to turn off
 */
void bsp_led_set(bool on);

/**
 * @brief Toggle LED state
 */
void bsp_led_toggle(void);

/**
 * @brief Check if board is running on battery
 *
 * @return true if on battery power
 */
bool bsp_is_battery_powered(void);

/**
 * @brief Get battery level percentage
 *
 * @return Battery percentage (0-100), or -1 if not available
 */
int bsp_get_battery_level(void);

/**
 * @brief Enter low power mode
 *
 * @param deep_sleep true for deep sleep, false for light sleep
 */
void bsp_enter_low_power(bool deep_sleep);

#ifdef __cplusplus
}
#endif
