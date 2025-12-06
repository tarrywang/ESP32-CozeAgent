#include <cstring>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "sdkconfig.h"
#include "freertos/queue.h"
#include "driver/i2c.h"
#include "driver/i2s_std.h"
#include "es8311.h"
#include "SensorLib.h"
#include "TouchDrvCST92xx.h"

// ===================== Touch I2C configuration =====================
#define I2C_MASTER_NUM_TOUCH    (i2c_port_t)1
#define I2C_MASTER_SDA_IO       (gpio_num_t)15
#define I2C_MASTER_SCL_IO       (gpio_num_t)14
#define Touch_INT               (gpio_num_t)11
#define Touch_RST               (gpio_num_t)40

// ===================== Audio I2C configuration =====================
#define I2C_NUM_AUDIO           (i2c_port_t)0

// ===================== Audio I2S configuration =====================
#define I2S_NUM                 (0)
#define I2S_MCK_IO              (GPIO_NUM_42)
#define I2S_BCK_IO              (GPIO_NUM_9)
#define I2S_WS_IO               (GPIO_NUM_45)
#define I2S_DO_IO               (GPIO_NUM_8)
#define I2S_DI_IO               (GPIO_NUM_10)
#define GPIO_OUTPUT_PA          (GPIO_NUM_46)

// ===================== Audio parameters =====================
#define SAMPLE_RATE             (16000)
#define MCLK_MULTIPLE           (384)
#define RECORD_DURATION_SEC     (10)
#define AUDIO_BUFFER_SIZE       (2400)
// Total samples: 16000 samples/sec * 2 channels * 2 bytes * 10 sec = 640000 bytes
#define TOTAL_AUDIO_SIZE        (SAMPLE_RATE * 2 * 2 * RECORD_DURATION_SEC)

static const char *TAG = "TouchAudio";

// Touch variables
TouchDrvCST92xx touch;
int16_t x[5], y[5];

// Audio handles
static i2s_chan_handle_t tx_handle = NULL;
static i2s_chan_handle_t rx_handle = NULL;

// Audio buffer (stored in PSRAM)
static uint8_t *audio_buffer = NULL;

// ===================== PA GPIO Init =====================
static void pa_gpio_init(void)
{
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = (1ULL << GPIO_OUTPUT_PA);
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&io_conf);
    gpio_set_level(GPIO_OUTPUT_PA, 1); // Enable PA
}

// ===================== ES8311 Codec Init =====================
static esp_err_t es8311_codec_init(void)
{
    const i2c_config_t es_i2c_cfg = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master = {.clk_speed = 100000},
        .clk_flags = 0,
    };
    ESP_RETURN_ON_ERROR(i2c_param_config(I2C_NUM_AUDIO, &es_i2c_cfg), TAG, "config i2c failed");
    ESP_RETURN_ON_ERROR(i2c_driver_install(I2C_NUM_AUDIO, I2C_MODE_MASTER, 0, 0, 0), TAG, "install i2c driver failed");

    es8311_handle_t es_handle = es8311_create(I2C_NUM_AUDIO, ES8311_ADDRRES_0);
    ESP_RETURN_ON_FALSE(es_handle, ESP_FAIL, TAG, "es8311 create failed");

    const es8311_clock_config_t es_clk = {
        .mclk_inverted = false,
        .sclk_inverted = false,
        .mclk_from_mclk_pin = true,
        .mclk_frequency = SAMPLE_RATE * MCLK_MULTIPLE,
        .sample_frequency = SAMPLE_RATE
    };

    ESP_ERROR_CHECK(es8311_init(es_handle, &es_clk, ES8311_RESOLUTION_16, ES8311_RESOLUTION_16));
    ESP_RETURN_ON_ERROR(es8311_sample_frequency_config(es_handle, SAMPLE_RATE * MCLK_MULTIPLE, SAMPLE_RATE), TAG, "set sample freq failed");
    ESP_RETURN_ON_ERROR(es8311_voice_volume_set(es_handle, 100, NULL), TAG, "set volume failed"); // Max volume
    ESP_RETURN_ON_ERROR(es8311_microphone_config(es_handle, false), TAG, "set mic config failed"); // Analog mic
    ESP_RETURN_ON_ERROR(es8311_microphone_gain_set(es_handle, ES8311_MIC_GAIN_MAX), TAG, "set mic gain failed"); // Max gain

    ESP_LOGI(TAG, "ES8311 configured: volume=100, mic_gain=MAX");
    return ESP_OK;
}

// ===================== I2S Driver Init =====================
static esp_err_t i2s_driver_init(void)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG((i2s_port_t)I2S_NUM, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &tx_handle, &rx_handle));

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_MCK_IO,
            .bclk = I2S_BCK_IO,
            .ws = I2S_WS_IO,
            .dout = I2S_DO_IO,
            .din = I2S_DI_IO,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    std_cfg.clk_cfg.mclk_multiple = (i2s_mclk_multiple_t)MCLK_MULTIPLE;

    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_handle, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_handle, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(tx_handle));
    ESP_ERROR_CHECK(i2s_channel_enable(rx_handle));

    return ESP_OK;
}

// ===================== Touch Setup =====================
static void setup_touch(void)
{
    uint8_t touchAddress = 0x5A;

    // Configure INT pin as input with pull-up
    gpio_config_t io_conf = {};
    io_conf.pin_bit_mask = (1ULL << Touch_INT);
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&io_conf);

    touch.setPins(Touch_RST, Touch_INT);
    touch.begin(I2C_MASTER_NUM_TOUCH, touchAddress, I2C_MASTER_SDA_IO, I2C_MASTER_SCL_IO);
    vTaskDelay(pdMS_TO_TICKS(100));
    touch.setMaxCoordinates(466, 466);
    touch.setMirrorXY(true, true);

    ESP_LOGI(TAG, "Touch initialized");
}

// ===================== Touch Read Task =====================
static void touch_read_task(void *arg)
{
    while (1)
    {
        if (touch.isPressed()) {
            uint8_t touched = touch.getPoint(x, y, 2);
            if (touched)
            {
                for (int i = 0; i < touched; ++i)
                {
                    ESP_LOGI(TAG, "Touch[%d]: X=%d Y=%d", i, x[i], y[i]);
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(30));
    }
}

// ===================== Audio Record and Playback Task =====================
static void audio_task(void *arg)
{
    size_t bytes_read = 0;
    size_t bytes_write = 0;
    size_t total_recorded = 0;
    uint8_t *temp_buffer = (uint8_t *)malloc(AUDIO_BUFFER_SIZE);

    if (!temp_buffer) {
        ESP_LOGE(TAG, "Failed to allocate temp buffer");
        vTaskDelete(NULL);
        return;
    }

    while (1) {
        // ===== Recording Phase =====
        ESP_LOGI(TAG, "=== Starting %d second recording ===", RECORD_DURATION_SEC);
        total_recorded = 0;

        while (total_recorded < TOTAL_AUDIO_SIZE) {
            size_t to_read = AUDIO_BUFFER_SIZE;
            if (total_recorded + to_read > TOTAL_AUDIO_SIZE) {
                to_read = TOTAL_AUDIO_SIZE - total_recorded;
            }

            esp_err_t ret = i2s_channel_read(rx_handle, temp_buffer, to_read, &bytes_read, 1000);
            if (ret == ESP_OK && bytes_read > 0) {
                memcpy(audio_buffer + total_recorded, temp_buffer, bytes_read);
                total_recorded += bytes_read;

                // Show progress every second
                if ((total_recorded % (SAMPLE_RATE * 4)) < AUDIO_BUFFER_SIZE) {
                    ESP_LOGI(TAG, "Recording: %zu/%d bytes (%.1f sec)",
                             total_recorded, TOTAL_AUDIO_SIZE,
                             (float)total_recorded / (SAMPLE_RATE * 4));
                }
            }
        }
        ESP_LOGI(TAG, "=== Recording complete: %zu bytes ===", total_recorded);

        // Check if audio data contains actual sound
        int16_t *samples = (int16_t *)audio_buffer;
        int32_t max_sample = 0;
        for (size_t i = 0; i < total_recorded / 2; i += 100) {
            int16_t sample = samples[i] > 0 ? samples[i] : -samples[i];
            if (sample > max_sample) max_sample = sample;
        }
        ESP_LOGI(TAG, "Max audio sample amplitude: %ld (32767 = full scale)", (long)max_sample);

        // ===== Wait 10 seconds =====
        ESP_LOGI(TAG, "=== Waiting %d seconds before playback ===", RECORD_DURATION_SEC);
        vTaskDelay(pdMS_TO_TICKS(RECORD_DURATION_SEC * 1000));

        // ===== Playback Phase =====
        ESP_LOGI(TAG, "=== Starting playback ===");
        size_t total_played = 0;

        while (total_played < total_recorded) {
            size_t to_write = AUDIO_BUFFER_SIZE;
            if (total_played + to_write > total_recorded) {
                to_write = total_recorded - total_played;
            }

            esp_err_t ret = i2s_channel_write(tx_handle, audio_buffer + total_played, to_write, &bytes_write, 1000);
            if (ret == ESP_OK && bytes_write > 0) {
                total_played += bytes_write;

                // Show progress every second
                if ((total_played % (SAMPLE_RATE * 4)) < AUDIO_BUFFER_SIZE) {
                    ESP_LOGI(TAG, "Playing: %zu/%zu bytes (%.1f sec)",
                             total_played, total_recorded,
                             (float)total_played / (SAMPLE_RATE * 4));
                }
            }
        }
        ESP_LOGI(TAG, "=== Playback complete ===");

        // Wait a bit before next recording cycle
        vTaskDelay(pdMS_TO_TICKS(2000));
    }

    free(temp_buffer);
    vTaskDelete(NULL);
}

// ===================== Main =====================
extern "C" void app_main()
{
    ESP_LOGI(TAG, "=== Touch + Audio Demo ===");

    // Allocate audio buffer in PSRAM
    audio_buffer = (uint8_t *)heap_caps_malloc(TOTAL_AUDIO_SIZE, MALLOC_CAP_SPIRAM);
    if (!audio_buffer) {
        ESP_LOGE(TAG, "Failed to allocate audio buffer in PSRAM, trying internal RAM");
        audio_buffer = (uint8_t *)malloc(TOTAL_AUDIO_SIZE);
        if (!audio_buffer) {
            ESP_LOGE(TAG, "Failed to allocate audio buffer");
            return;
        }
    }
    ESP_LOGI(TAG, "Audio buffer allocated: %d bytes", TOTAL_AUDIO_SIZE);

    // Initialize PA
    pa_gpio_init();

    // Initialize I2S
    if (i2s_driver_init() != ESP_OK) {
        ESP_LOGE(TAG, "I2S driver init failed");
        return;
    }
    ESP_LOGI(TAG, "I2S driver initialized");

    // Initialize ES8311 codec
    if (es8311_codec_init() != ESP_OK) {
        ESP_LOGE(TAG, "ES8311 codec init failed");
        return;
    }
    ESP_LOGI(TAG, "ES8311 codec initialized");

    // Initialize touch
    setup_touch();

    // Create tasks
    xTaskCreate(touch_read_task, "touch_task", 4096, NULL, 10, NULL);
    xTaskCreate(audio_task, "audio_task", 8192, NULL, 5, NULL);

    ESP_LOGI(TAG, "All tasks started");
}
