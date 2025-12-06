/**
 * @file pcf85063_driver.h
 * @brief PCF85063 RTC driver C API
 */

#ifndef PCF85063_DRIVER_H
#define PCF85063_DRIVER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief RTC date/time structure
 */
typedef struct {
    uint16_t year;      // 2000-2099
    uint8_t  month;     // 1-12
    uint8_t  day;       // 1-31
    uint8_t  weekday;   // 0=Sunday, 1=Monday, ..., 6=Saturday
    uint8_t  hour;      // 0-23
    uint8_t  minute;    // 0-59
    uint8_t  second;    // 0-59
} pcf85063_datetime_t;

/**
 * @brief Initialize PCF85063 RTC
 * @param i2c_port I2C port number
 * @param sda_pin SDA GPIO pin
 * @param scl_pin SCL GPIO pin
 * @return ESP_OK on success
 */
esp_err_t pcf85063_init(int i2c_port, int sda_pin, int scl_pin);

/**
 * @brief Get current date and time
 * @param dt Pointer to datetime structure to fill
 * @return ESP_OK on success
 */
esp_err_t pcf85063_get_datetime(pcf85063_datetime_t *dt);

/**
 * @brief Set date and time
 * @param dt Pointer to datetime structure
 * @return ESP_OK on success
 */
esp_err_t pcf85063_set_datetime(const pcf85063_datetime_t *dt);

/**
 * @brief Get formatted time string (HH:MM:SS)
 * @param buf Buffer to write to (at least 9 bytes)
 * @param buf_size Buffer size
 * @return ESP_OK on success
 */
esp_err_t pcf85063_get_time_str(char *buf, size_t buf_size);

/**
 * @brief Get formatted date string (YYYY-MM-DD)
 * @param buf Buffer to write to (at least 11 bytes)
 * @param buf_size Buffer size
 * @return ESP_OK on success
 */
esp_err_t pcf85063_get_date_str(char *buf, size_t buf_size);

/**
 * @brief Get weekday name
 * @param weekday Weekday number (0-6)
 * @return Weekday name string
 */
const char* pcf85063_get_weekday_name(uint8_t weekday);

#ifdef __cplusplus
}
#endif

#endif // PCF85063_DRIVER_H
