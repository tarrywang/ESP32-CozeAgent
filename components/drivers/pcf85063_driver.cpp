/**
 * @file pcf85063_driver.cpp
 * @brief PCF85063 RTC driver implementation
 */

#include "pcf85063_driver.h"
#include "i2c_bus.h"
#include "esp_log.h"
#include <stdio.h>

static const char *TAG = "pcf85063";

// PCF85063 I2C address
#define PCF85063_ADDR       0x51

// PCF85063 register addresses
#define PCF85063_REG_CTRL1      0x00
#define PCF85063_REG_CTRL2      0x01
#define PCF85063_REG_OFFSET     0x02
#define PCF85063_REG_RAM        0x03
#define PCF85063_REG_SECONDS    0x04
#define PCF85063_REG_MINUTES    0x05
#define PCF85063_REG_HOURS      0x06
#define PCF85063_REG_DAYS       0x07
#define PCF85063_REG_WEEKDAYS   0x08
#define PCF85063_REG_MONTHS     0x09
#define PCF85063_REG_YEARS      0x0A

static bool s_initialized = false;

// BCD to decimal conversion
static uint8_t bcd_to_dec(uint8_t bcd)
{
    return ((bcd >> 4) * 10) + (bcd & 0x0F);
}

// Decimal to BCD conversion
static uint8_t dec_to_bcd(uint8_t dec)
{
    return ((dec / 10) << 4) | (dec % 10);
}

static const char* weekday_names[] = {
    "Sunday", "Monday", "Tuesday", "Wednesday",
    "Thursday", "Friday", "Saturday"
};

extern "C" {

esp_err_t pcf85063_init(int i2c_port, int sda_pin, int scl_pin)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "PCF85063 already initialized");
        return ESP_OK;
    }

    // Initialize I2C bus if not already done
    if (!i2c_bus_is_initialized()) {
        esp_err_t ret = i2c_bus_init((i2c_port_t)i2c_port, sda_pin, scl_pin, 100000);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize I2C bus");
            return ret;
        }
    }

    // Read control register to verify communication
    uint8_t ctrl1 = 0;
    if (i2c_bus_read(PCF85063_ADDR, PCF85063_REG_CTRL1, &ctrl1, 1) != 0) {
        ESP_LOGE(TAG, "Failed to communicate with PCF85063");
        return ESP_FAIL;
    }

    // Clear stop bit if set (bit 5)
    if (ctrl1 & 0x20) {
        ctrl1 &= ~0x20;
        i2c_bus_write(PCF85063_ADDR, PCF85063_REG_CTRL1, &ctrl1, 1);
    }

    s_initialized = true;
    ESP_LOGI(TAG, "PCF85063 initialized successfully");

    // Log current time
    pcf85063_datetime_t dt;
    if (pcf85063_get_datetime(&dt) == ESP_OK) {
        ESP_LOGI(TAG, "Current time: %04d-%02d-%02d %02d:%02d:%02d",
                 dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second);
    }

    return ESP_OK;
}

esp_err_t pcf85063_get_datetime(pcf85063_datetime_t *dt)
{
    if (!s_initialized || dt == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t buf[7];
    if (i2c_bus_read(PCF85063_ADDR, PCF85063_REG_SECONDS, buf, 7) != 0) {
        ESP_LOGE(TAG, "Failed to read datetime");
        return ESP_FAIL;
    }

    dt->second = bcd_to_dec(buf[0] & 0x7F);  // Mask OS bit
    dt->minute = bcd_to_dec(buf[1] & 0x7F);
    dt->hour = bcd_to_dec(buf[2] & 0x3F);    // 24-hour format
    dt->day = bcd_to_dec(buf[3] & 0x3F);
    dt->weekday = buf[4] & 0x07;
    dt->month = bcd_to_dec(buf[5] & 0x1F);
    dt->year = 2000 + bcd_to_dec(buf[6]);

    return ESP_OK;
}

esp_err_t pcf85063_set_datetime(const pcf85063_datetime_t *dt)
{
    if (!s_initialized || dt == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    // Validate input
    if (dt->year < 2000 || dt->year > 2099 ||
        dt->month < 1 || dt->month > 12 ||
        dt->day < 1 || dt->day > 31 ||
        dt->hour > 23 || dt->minute > 59 || dt->second > 59) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t buf[7];
    buf[0] = dec_to_bcd(dt->second);
    buf[1] = dec_to_bcd(dt->minute);
    buf[2] = dec_to_bcd(dt->hour);
    buf[3] = dec_to_bcd(dt->day);
    buf[4] = dt->weekday & 0x07;
    buf[5] = dec_to_bcd(dt->month);
    buf[6] = dec_to_bcd(dt->year - 2000);

    // Stop RTC before writing
    uint8_t ctrl1 = 0x20;  // Set STOP bit
    if (i2c_bus_write(PCF85063_ADDR, PCF85063_REG_CTRL1, &ctrl1, 1) != 0) {
        return ESP_FAIL;
    }

    // Write datetime
    if (i2c_bus_write(PCF85063_ADDR, PCF85063_REG_SECONDS, buf, 7) != 0) {
        return ESP_FAIL;
    }

    // Start RTC
    ctrl1 = 0x00;  // Clear STOP bit
    if (i2c_bus_write(PCF85063_ADDR, PCF85063_REG_CTRL1, &ctrl1, 1) != 0) {
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Time set to: %04d-%02d-%02d %02d:%02d:%02d",
             dt->year, dt->month, dt->day, dt->hour, dt->minute, dt->second);

    return ESP_OK;
}

esp_err_t pcf85063_get_time_str(char *buf, size_t buf_size)
{
    if (buf == NULL || buf_size < 9) {
        return ESP_ERR_INVALID_ARG;
    }

    pcf85063_datetime_t dt;
    esp_err_t ret = pcf85063_get_datetime(&dt);
    if (ret != ESP_OK) {
        return ret;
    }

    snprintf(buf, buf_size, "%02d:%02d:%02d", dt.hour, dt.minute, dt.second);
    return ESP_OK;
}

esp_err_t pcf85063_get_date_str(char *buf, size_t buf_size)
{
    if (buf == NULL || buf_size < 11) {
        return ESP_ERR_INVALID_ARG;
    }

    pcf85063_datetime_t dt;
    esp_err_t ret = pcf85063_get_datetime(&dt);
    if (ret != ESP_OK) {
        return ret;
    }

    snprintf(buf, buf_size, "%04d-%02d-%02d", dt.year, dt.month, dt.day);
    return ESP_OK;
}

const char* pcf85063_get_weekday_name(uint8_t weekday)
{
    if (weekday > 6) {
        return "Unknown";
    }
    return weekday_names[weekday];
}

} // extern "C"
