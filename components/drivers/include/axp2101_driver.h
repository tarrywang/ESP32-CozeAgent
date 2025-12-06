/**
 * @file axp2101_driver.h
 * @brief AXP2101 PMU driver C API wrapper
 */

#ifndef AXP2101_DRIVER_H
#define AXP2101_DRIVER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief AXP2101 battery information structure
 */
typedef struct {
    uint8_t  percent;           // Battery percentage (0-100)
    uint16_t voltage_mv;        // Battery voltage in mV
    bool     is_charging;       // True if charging
    bool     is_battery_present; // True if battery connected
    float    temperature_c;     // PMU temperature in Celsius
    uint16_t vbus_voltage_mv;   // USB voltage in mV
    uint16_t sys_voltage_mv;    // System voltage in mV
} axp2101_info_t;

/**
 * @brief Initialize AXP2101 PMU
 * @param i2c_port I2C port number
 * @param sda_pin SDA GPIO pin
 * @param scl_pin SCL GPIO pin
 * @return ESP_OK on success
 */
esp_err_t axp2101_init(int i2c_port, int sda_pin, int scl_pin);

/**
 * @brief Get battery percentage
 * @return Battery percentage (0-100), or -1 on error
 */
int axp2101_get_battery_percent(void);

/**
 * @brief Get battery voltage
 * @return Battery voltage in mV, or 0 on error
 */
uint16_t axp2101_get_battery_voltage(void);

/**
 * @brief Check if battery is charging
 * @return true if charging
 */
bool axp2101_is_charging(void);

/**
 * @brief Check if battery is connected
 * @return true if battery present
 */
bool axp2101_is_battery_present(void);

/**
 * @brief Get PMU temperature
 * @return Temperature in Celsius
 */
float axp2101_get_temperature(void);

/**
 * @brief Get USB VBUS voltage
 * @return VBUS voltage in mV
 */
uint16_t axp2101_get_vbus_voltage(void);

/**
 * @brief Get all battery/PMU information at once
 * @param info Pointer to info structure to fill
 * @return ESP_OK on success
 */
esp_err_t axp2101_get_info(axp2101_info_t *info);

#ifdef __cplusplus
}
#endif

#endif // AXP2101_DRIVER_H
