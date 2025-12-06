/**
 * @file bsp_board.c
 * @brief Board Support Package main implementation
 *
 * Initializes and coordinates all board peripherals.
 */

#include "bsp_board.h"
#include "bsp_i2s.h"
#include "bsp_display.h"
#include "bsp_touch.h"
#include "bsp_button.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_sleep.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/spi_master.h"

static const char *TAG = "BSP_BOARD";

// ============================================
// Private Variables
// ============================================

static bool s_board_initialized = false;
static bool s_led_state = false;

// ============================================
// Private Functions
// ============================================

/**
 * @brief Initialize GPIO for LED and other simple I/O
 */
static esp_err_t bsp_gpio_init(void)
{
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << BSP_LED_GPIO),
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
    };

    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure LED GPIO: %s", esp_err_to_name(ret));
        return ret;
    }

    // Turn LED off initially
    gpio_set_level(BSP_LED_GPIO, 0);

    return ESP_OK;
}

/**
 * @brief Initialize I2C bus for touch and PMU
 */
static esp_err_t bsp_i2c_init(void)
{
    i2c_config_t i2c_conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = BSP_TOUCH_I2C_SDA,
        .scl_io_num = BSP_TOUCH_I2C_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 400000,  // 400kHz I2C clock
    };

    esp_err_t ret = i2c_param_config(BSP_TOUCH_I2C_NUM, &i2c_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C param config failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = i2c_driver_install(BSP_TOUCH_I2C_NUM, i2c_conf.mode, 0, 0, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C driver install failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "I2C bus initialized");
    return ESP_OK;
}

/**
 * @brief Initialize SPI bus for display
 */
static esp_err_t bsp_spi_init(void)
{
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = BSP_LCD_SPI_MOSI,
        .miso_io_num = -1,  // Display is write-only
        .sclk_io_num = BSP_LCD_SPI_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = BSP_LCD_WIDTH * BSP_LCD_HEIGHT * 2 + 8,
        .flags = SPICOMMON_BUSFLAG_MASTER,
    };

    esp_err_t ret = spi_bus_initialize(BSP_LCD_SPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI bus init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "SPI bus initialized");
    return ESP_OK;
}

// ============================================
// Public Functions
// ============================================

esp_err_t bsp_board_init(void)
{
    if (s_board_initialized) {
        ESP_LOGW(TAG, "Board already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing board support package...");

    esp_err_t ret;

    // Step 1: Initialize GPIO
    ret = bsp_gpio_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GPIO init failed");
        return ret;
    }
    ESP_LOGI(TAG, "GPIO initialized");

    // Step 2: Initialize I2C bus
    ret = bsp_i2c_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C init failed");
        return ret;
    }

    // Step 3: Initialize SPI bus
    ret = bsp_spi_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI init failed");
        return ret;
    }

    // Step 4: Initialize I2S microphone
    bsp_i2s_mic_config_t mic_cfg = BSP_I2S_MIC_DEFAULT_CONFIG();
    ret = bsp_i2s_mic_init(&mic_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2S microphone init failed");
        return ret;
    }
    ESP_LOGI(TAG, "I2S microphone initialized");

    // Step 5: Initialize I2S speaker
    bsp_i2s_spk_config_t spk_cfg = BSP_I2S_SPK_DEFAULT_CONFIG();
    ret = bsp_i2s_spk_init(&spk_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2S speaker init failed");
        return ret;
    }
    ESP_LOGI(TAG, "I2S speaker initialized");

    // Step 6: Initialize display
    bsp_display_config_t disp_cfg = BSP_DISPLAY_DEFAULT_CONFIG();
    ret = bsp_display_init(&disp_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Display init failed");
        return ret;
    }
    ESP_LOGI(TAG, "Display initialized");

    // Step 7: Initialize touch (non-critical - continue if fails)
    bsp_touch_config_t touch_cfg = BSP_TOUCH_DEFAULT_CONFIG();
    ret = bsp_touch_init(&touch_cfg);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Touch init failed (non-critical, continuing): %s", esp_err_to_name(ret));
        // Don't return error - touch is not critical for voice agent
    } else {
        ESP_LOGI(TAG, "Touch initialized");
    }

    // Step 8: Initialize buttons
    bsp_button_config_t btn_cfg = BSP_BUTTON_DEFAULT_CONFIG();
    ret = bsp_button_init(&btn_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Button init failed");
        return ret;
    }
    ESP_LOGI(TAG, "Buttons initialized");

    // Start LVGL tick timer
    ret = bsp_display_start_tick_timer();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LVGL tick timer start failed");
        return ret;
    }

    s_board_initialized = true;
    ESP_LOGI(TAG, "Board support package initialized successfully");

    // Flash LED to indicate successful init
    for (int i = 0; i < 3; i++) {
        bsp_led_set(true);
        vTaskDelay(pdMS_TO_TICKS(100));
        bsp_led_set(false);
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    return ESP_OK;
}

esp_err_t bsp_board_deinit(void)
{
    if (!s_board_initialized) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Deinitializing board...");

    bsp_display_stop_tick_timer();
    bsp_button_deinit();
    bsp_touch_deinit();
    bsp_display_deinit();
    bsp_i2s_spk_deinit();
    bsp_i2s_mic_deinit();

    // Deinitialize buses
    spi_bus_free(BSP_LCD_SPI_HOST);
    i2c_driver_delete(BSP_TOUCH_I2C_NUM);

    s_board_initialized = false;
    ESP_LOGI(TAG, "Board deinitialized");

    return ESP_OK;
}

const char *bsp_board_get_version(void)
{
    return "Waveshare ESP32-S3 1.75-inch AMOLED v1.0";
}

void bsp_led_set(bool on)
{
    s_led_state = on;
    gpio_set_level(BSP_LED_GPIO, on ? 1 : 0);
}

void bsp_led_toggle(void)
{
    bsp_led_set(!s_led_state);
}

bool bsp_is_battery_powered(void)
{
    // TODO: Implement AXP2101 PMU check
    return false;
}

int bsp_get_battery_level(void)
{
    // TODO: Implement AXP2101 PMU battery level reading
    return -1;  // Not available
}

void bsp_enter_low_power(bool deep_sleep)
{
    ESP_LOGI(TAG, "Entering low power mode (deep_sleep=%d)", deep_sleep);

    // Turn off peripherals
    bsp_display_power(false);
    bsp_i2s_mic_stop();
    bsp_i2s_spk_stop();

    if (deep_sleep) {
        // Configure wake-up sources
        esp_sleep_enable_ext0_wakeup(BSP_BUTTON_BOOT, 0);  // Wake on BOOT button

        ESP_LOGI(TAG, "Entering deep sleep...");
        esp_deep_sleep_start();
    } else {
        // Light sleep
        ESP_LOGI(TAG, "Entering light sleep...");
        esp_light_sleep_start();
    }
}
