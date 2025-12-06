/**
 * @file bsp_i2s.c
 * @brief I2S driver implementation for microphone and speaker
 *
 * Uses ESP-IDF v5.x I2S standard mode driver.
 */

#include "bsp_i2s.h"
#include "bsp_board.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"

static const char *TAG = "BSP_I2S";

// ============================================
// Private Variables
// ============================================

// Microphone channel
static i2s_chan_handle_t s_mic_handle = NULL;
static bool s_mic_running = false;
static SemaphoreHandle_t s_mic_mutex = NULL;

// Speaker channel
static i2s_chan_handle_t s_spk_handle = NULL;
static bool s_spk_running = false;
static bool s_spk_muted = false;
static uint8_t s_spk_volume = 80;
static SemaphoreHandle_t s_spk_mutex = NULL;

// ============================================
// Private Functions
// ============================================

/**
 * @brief Apply volume scaling to audio samples
 */
static void apply_volume(int16_t *samples, size_t sample_count, uint8_t volume)
{
    if (volume >= 100) return;  // No change needed
    if (volume == 0) {
        memset(samples, 0, sample_count * sizeof(int16_t));
        return;
    }

    // Volume scaling factor (volume/100)
    for (size_t i = 0; i < sample_count; i++) {
        samples[i] = (int16_t)((int32_t)samples[i] * volume / 100);
    }
}

// ============================================
// Public Functions - Microphone
// ============================================

esp_err_t bsp_i2s_mic_init(const bsp_i2s_mic_config_t *config)
{
    if (s_mic_handle != NULL) {
        ESP_LOGW(TAG, "Microphone already initialized");
        return ESP_OK;
    }

    // Use default config if not provided
    bsp_i2s_mic_config_t cfg;
    if (config == NULL) {
        cfg = (bsp_i2s_mic_config_t)BSP_I2S_MIC_DEFAULT_CONFIG();
    } else {
        cfg = *config;
    }

    ESP_LOGI(TAG, "Initializing I2S microphone: %lu Hz, %d-bit, %d channels",
             cfg.sample_rate, cfg.bits_per_sample, cfg.channels);

    // Create mutex
    s_mic_mutex = xSemaphoreCreateMutex();
    if (s_mic_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mic mutex");
        return ESP_ERR_NO_MEM;
    }

    // Channel configuration
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(BSP_I2S_MIC_NUM, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = cfg.buffer_count;
    chan_cfg.dma_frame_num = cfg.buffer_size / (cfg.bits_per_sample / 8);

    esp_err_t ret = i2s_new_channel(&chan_cfg, NULL, &s_mic_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create I2S mic channel: %s", esp_err_to_name(ret));
        vSemaphoreDelete(s_mic_mutex);
        s_mic_mutex = NULL;
        return ret;
    }

    // Standard mode configuration for microphone (RX only)
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(cfg.sample_rate),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
            cfg.bits_per_sample == 16 ? I2S_DATA_BIT_WIDTH_16BIT : I2S_DATA_BIT_WIDTH_32BIT,
            cfg.channels == 1 ? I2S_SLOT_MODE_MONO : I2S_SLOT_MODE_STEREO
        ),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = BSP_I2S_MIC_BCLK,
            .ws = BSP_I2S_MIC_WS,
            .dout = I2S_GPIO_UNUSED,
            .din = BSP_I2S_MIC_DATA_IN,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    ret = i2s_channel_init_std_mode(s_mic_handle, &std_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init I2S mic std mode: %s", esp_err_to_name(ret));
        i2s_del_channel(s_mic_handle);
        s_mic_handle = NULL;
        vSemaphoreDelete(s_mic_mutex);
        s_mic_mutex = NULL;
        return ret;
    }

    ESP_LOGI(TAG, "I2S microphone initialized");
    return ESP_OK;
}

esp_err_t bsp_i2s_mic_deinit(void)
{
    if (s_mic_handle == NULL) {
        return ESP_OK;
    }

    if (s_mic_running) {
        bsp_i2s_mic_stop();
    }

    i2s_del_channel(s_mic_handle);
    s_mic_handle = NULL;

    if (s_mic_mutex) {
        vSemaphoreDelete(s_mic_mutex);
        s_mic_mutex = NULL;
    }

    ESP_LOGI(TAG, "I2S microphone deinitialized");
    return ESP_OK;
}

esp_err_t bsp_i2s_mic_start(void)
{
    if (s_mic_handle == NULL) {
        ESP_LOGE(TAG, "Microphone not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (s_mic_running) {
        return ESP_OK;
    }

    xSemaphoreTake(s_mic_mutex, portMAX_DELAY);
    esp_err_t ret = i2s_channel_enable(s_mic_handle);
    if (ret == ESP_OK) {
        s_mic_running = true;
        ESP_LOGI(TAG, "Microphone started");
    } else {
        ESP_LOGE(TAG, "Failed to start microphone: %s", esp_err_to_name(ret));
    }
    xSemaphoreGive(s_mic_mutex);

    return ret;
}

esp_err_t bsp_i2s_mic_stop(void)
{
    if (s_mic_handle == NULL || !s_mic_running) {
        return ESP_OK;
    }

    xSemaphoreTake(s_mic_mutex, portMAX_DELAY);
    esp_err_t ret = i2s_channel_disable(s_mic_handle);
    if (ret == ESP_OK) {
        s_mic_running = false;
        ESP_LOGI(TAG, "Microphone stopped");
    }
    xSemaphoreGive(s_mic_mutex);

    return ret;
}

esp_err_t bsp_i2s_mic_read(void *buffer, size_t buffer_size,
                           size_t *bytes_read, uint32_t timeout_ms)
{
    if (s_mic_handle == NULL) {
        ESP_LOGE(TAG, "Microphone not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (!s_mic_running) {
        ESP_LOGE(TAG, "Microphone not running");
        return ESP_ERR_INVALID_STATE;
    }

    if (buffer == NULL || bytes_read == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    return i2s_channel_read(s_mic_handle, buffer, buffer_size, bytes_read,
                            pdMS_TO_TICKS(timeout_ms));
}

i2s_chan_handle_t bsp_i2s_mic_get_handle(void)
{
    return s_mic_handle;
}

// ============================================
// Public Functions - Speaker
// ============================================

esp_err_t bsp_i2s_spk_init(const bsp_i2s_spk_config_t *config)
{
    if (s_spk_handle != NULL) {
        ESP_LOGW(TAG, "Speaker already initialized");
        return ESP_OK;
    }

    // Use default config if not provided
    bsp_i2s_spk_config_t cfg;
    if (config == NULL) {
        cfg = (bsp_i2s_spk_config_t)BSP_I2S_SPK_DEFAULT_CONFIG();
    } else {
        cfg = *config;
    }

    s_spk_volume = cfg.volume;

    ESP_LOGI(TAG, "Initializing I2S speaker: %lu Hz, %d-bit, %d channels, vol=%d",
             cfg.sample_rate, cfg.bits_per_sample, cfg.channels, cfg.volume);

    // Create mutex
    s_spk_mutex = xSemaphoreCreateMutex();
    if (s_spk_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create speaker mutex");
        return ESP_ERR_NO_MEM;
    }

    // Channel configuration
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(BSP_I2S_SPK_NUM, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = cfg.buffer_count;
    chan_cfg.dma_frame_num = cfg.buffer_size / (cfg.bits_per_sample / 8);

    esp_err_t ret = i2s_new_channel(&chan_cfg, &s_spk_handle, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create I2S speaker channel: %s", esp_err_to_name(ret));
        vSemaphoreDelete(s_spk_mutex);
        s_spk_mutex = NULL;
        return ret;
    }

    // Standard mode configuration for speaker (TX only)
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(cfg.sample_rate),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
            cfg.bits_per_sample == 16 ? I2S_DATA_BIT_WIDTH_16BIT : I2S_DATA_BIT_WIDTH_32BIT,
            cfg.channels == 1 ? I2S_SLOT_MODE_MONO : I2S_SLOT_MODE_STEREO
        ),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = BSP_I2S_SPK_BCLK,
            .ws = BSP_I2S_SPK_WS,
            .dout = BSP_I2S_SPK_DATA_OUT,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    ret = i2s_channel_init_std_mode(s_spk_handle, &std_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init I2S speaker std mode: %s", esp_err_to_name(ret));
        i2s_del_channel(s_spk_handle);
        s_spk_handle = NULL;
        vSemaphoreDelete(s_spk_mutex);
        s_spk_mutex = NULL;
        return ret;
    }

    ESP_LOGI(TAG, "I2S speaker initialized");
    return ESP_OK;
}

esp_err_t bsp_i2s_spk_deinit(void)
{
    if (s_spk_handle == NULL) {
        return ESP_OK;
    }

    if (s_spk_running) {
        bsp_i2s_spk_stop();
    }

    i2s_del_channel(s_spk_handle);
    s_spk_handle = NULL;

    if (s_spk_mutex) {
        vSemaphoreDelete(s_spk_mutex);
        s_spk_mutex = NULL;
    }

    ESP_LOGI(TAG, "I2S speaker deinitialized");
    return ESP_OK;
}

esp_err_t bsp_i2s_spk_start(void)
{
    if (s_spk_handle == NULL) {
        ESP_LOGE(TAG, "Speaker not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (s_spk_running) {
        return ESP_OK;
    }

    xSemaphoreTake(s_spk_mutex, portMAX_DELAY);
    esp_err_t ret = i2s_channel_enable(s_spk_handle);
    if (ret == ESP_OK) {
        s_spk_running = true;
        ESP_LOGI(TAG, "Speaker started");
    } else {
        ESP_LOGE(TAG, "Failed to start speaker: %s", esp_err_to_name(ret));
    }
    xSemaphoreGive(s_spk_mutex);

    return ret;
}

esp_err_t bsp_i2s_spk_stop(void)
{
    if (s_spk_handle == NULL || !s_spk_running) {
        return ESP_OK;
    }

    xSemaphoreTake(s_spk_mutex, portMAX_DELAY);
    esp_err_t ret = i2s_channel_disable(s_spk_handle);
    if (ret == ESP_OK) {
        s_spk_running = false;
        ESP_LOGI(TAG, "Speaker stopped");
    }
    xSemaphoreGive(s_spk_mutex);

    return ret;
}

esp_err_t bsp_i2s_spk_write(const void *buffer, size_t buffer_size,
                            size_t *bytes_written, uint32_t timeout_ms)
{
    if (s_spk_handle == NULL) {
        ESP_LOGE(TAG, "Speaker not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (!s_spk_running) {
        ESP_LOGE(TAG, "Speaker not running");
        return ESP_ERR_INVALID_STATE;
    }

    if (buffer == NULL || bytes_written == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // If muted, just report success without writing
    if (s_spk_muted) {
        *bytes_written = buffer_size;
        return ESP_OK;
    }

    // Apply volume if not at max
    if (s_spk_volume < 100) {
        // Create a copy of the buffer to apply volume
        int16_t *temp_buffer = malloc(buffer_size);
        if (temp_buffer == NULL) {
            ESP_LOGE(TAG, "Failed to allocate temp buffer for volume");
            return ESP_ERR_NO_MEM;
        }

        memcpy(temp_buffer, buffer, buffer_size);
        apply_volume(temp_buffer, buffer_size / sizeof(int16_t), s_spk_volume);

        esp_err_t ret = i2s_channel_write(s_spk_handle, temp_buffer, buffer_size,
                                          bytes_written, pdMS_TO_TICKS(timeout_ms));
        free(temp_buffer);
        return ret;
    }

    return i2s_channel_write(s_spk_handle, buffer, buffer_size,
                             bytes_written, pdMS_TO_TICKS(timeout_ms));
}

esp_err_t bsp_i2s_spk_set_volume(uint8_t volume)
{
    if (volume > 100) {
        volume = 100;
    }

    s_spk_volume = volume;
    ESP_LOGI(TAG, "Speaker volume set to %d", volume);
    return ESP_OK;
}

uint8_t bsp_i2s_spk_get_volume(void)
{
    return s_spk_volume;
}

esp_err_t bsp_i2s_spk_mute(bool mute)
{
    s_spk_muted = mute;
    ESP_LOGI(TAG, "Speaker %s", mute ? "muted" : "unmuted");
    return ESP_OK;
}

bool bsp_i2s_spk_is_muted(void)
{
    return s_spk_muted;
}

esp_err_t bsp_i2s_spk_clear_buffer(void)
{
    if (s_spk_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    // Write silence to clear the buffer
    static const uint8_t silence[1024] = {0};
    size_t bytes_written;

    for (int i = 0; i < 4; i++) {
        i2s_channel_write(s_spk_handle, silence, sizeof(silence),
                          &bytes_written, pdMS_TO_TICKS(10));
    }

    return ESP_OK;
}

i2s_chan_handle_t bsp_i2s_spk_get_handle(void)
{
    return s_spk_handle;
}
