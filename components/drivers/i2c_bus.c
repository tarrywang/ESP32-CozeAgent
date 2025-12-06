/**
 * @file i2c_bus.c
 * @brief Shared I2C bus driver implementation (new I2C driver API)
 */

#include "i2c_bus.h"
#include "esp_log.h"
#include "esp_check.h"
#include "bsp/esp32_s3_touch_amoled_1_75.h"
#include <string.h>

static const char *TAG = "i2c_bus";

static bool s_initialized = false;
static i2c_master_bus_handle_t s_bus_handle = NULL;
static uint32_t s_bus_freq_hz = 0;
static i2c_port_t s_i2c_port = I2C_NUM_0;

typedef struct {
    uint8_t addr;
    i2c_master_dev_handle_t handle;
} i2c_device_entry_t;

static i2c_device_entry_t s_devices[4];

static i2c_master_dev_handle_t get_device_handle(uint8_t dev_addr)
{
    for (size_t i = 0; i < sizeof(s_devices) / sizeof(s_devices[0]); ++i) {
        if (s_devices[i].handle && s_devices[i].addr == dev_addr) {
            return s_devices[i].handle;
        }
    }

    for (size_t i = 0; i < sizeof(s_devices) / sizeof(s_devices[0]); ++i) {
        if (s_devices[i].handle == NULL) {
            i2c_master_dev_handle_t handle = NULL;
            i2c_device_config_t dev_cfg = {
                .dev_addr_length = I2C_ADDR_BIT_LEN_7,
                .device_address = dev_addr,
                .scl_speed_hz = s_bus_freq_hz ? s_bus_freq_hz : CONFIG_BSP_I2C_CLK_SPEED_HZ,
            };
            if (i2c_master_bus_add_device(s_bus_handle, &dev_cfg, &handle) != ESP_OK) {
                ESP_LOGE(TAG, "Failed to add I2C device 0x%02X", dev_addr);
                return NULL;
            }
            s_devices[i].addr = dev_addr;
            s_devices[i].handle = handle;
            return handle;
        }
    }

    ESP_LOGE(TAG, "No free slots to register I2C device 0x%02X", dev_addr);
    return NULL;
}

esp_err_t i2c_bus_init(i2c_port_t i2c_port, int sda_pin, int scl_pin, uint32_t freq_hz)
{
    if (s_initialized) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(bsp_i2c_init(), TAG, "Failed to init BSP I2C bus");

    s_bus_handle = bsp_i2c_get_handle();
    if (s_bus_handle == NULL) {
        ESP_LOGE(TAG, "BSP I2C handle is NULL");
        return ESP_FAIL;
    }

    s_i2c_port = i2c_port;
    s_bus_freq_hz = freq_hz;
    s_initialized = true;

    ESP_LOGI(TAG, "I2C bus ready on port %d (SDA=%d, SCL=%d)", i2c_port, sda_pin, scl_pin);
    return ESP_OK;
}

bool i2c_bus_is_initialized(void)
{
    return s_initialized;
}

i2c_port_t i2c_bus_get_port(void)
{
    return s_i2c_port;
}

int i2c_bus_read(uint8_t dev_addr, uint8_t reg_addr, uint8_t *data, uint8_t len)
{
    if (!s_initialized || data == NULL || len == 0) {
        ESP_LOGE(TAG, "I2C bus not ready or invalid args");
        return -1;
    }

    i2c_master_dev_handle_t dev = get_device_handle(dev_addr);
    if (dev == NULL) {
        return -1;
    }

    esp_err_t ret = i2c_master_transmit(dev, &reg_addr, 1, pdMS_TO_TICKS(1000));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C write reg addr failed: 0x%02X (%s)", dev_addr, esp_err_to_name(ret));
        return -1;
    }

    ret = i2c_master_receive(dev, data, len, pdMS_TO_TICKS(1000));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C read failed: 0x%02X (%s)", dev_addr, esp_err_to_name(ret));
        return -1;
    }

    return 0;
}

int i2c_bus_write(uint8_t dev_addr, uint8_t reg_addr, uint8_t *data, uint8_t len)
{
    if (!s_initialized || data == NULL) {
        ESP_LOGE(TAG, "I2C bus not ready or invalid args");
        return -1;
    }

    i2c_master_dev_handle_t dev = get_device_handle(dev_addr);
    if (dev == NULL) {
        return -1;
    }

    uint8_t buffer[1 + len];
    buffer[0] = reg_addr;
    memcpy(&buffer[1], data, len);

    esp_err_t ret = i2c_master_transmit(dev, buffer, sizeof(buffer), pdMS_TO_TICKS(1000));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C write failed: 0x%02X (%s)", dev_addr, esp_err_to_name(ret));
        return -1;
    }

    return 0;
}
