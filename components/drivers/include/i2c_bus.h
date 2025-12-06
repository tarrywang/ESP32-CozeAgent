/**
 * @file i2c_bus.h
 * @brief Shared I2C bus driver for sensors
 */

#ifndef I2C_BUS_H
#define I2C_BUS_H

#include <stdint.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize I2C master bus
 * @param i2c_port I2C port number
 * @param sda_pin SDA GPIO pin
 * @param scl_pin SCL GPIO pin
 * @param freq_hz I2C frequency in Hz
 * @return ESP_OK on success
 */
esp_err_t i2c_bus_init(i2c_port_t i2c_port, int sda_pin, int scl_pin, uint32_t freq_hz);

/**
 * @brief Check if I2C bus is initialized
 * @return true if initialized
 */
bool i2c_bus_is_initialized(void);

/**
 * @brief Get the I2C port number
 * @return I2C port number
 */
i2c_port_t i2c_bus_get_port(void);

/**
 * @brief Read bytes from I2C device
 * @param dev_addr Device I2C address (7-bit)
 * @param reg_addr Register address
 * @param data Buffer to read into
 * @param len Number of bytes to read
 * @return 0 on success, -1 on error
 */
int i2c_bus_read(uint8_t dev_addr, uint8_t reg_addr, uint8_t *data, uint8_t len);

/**
 * @brief Write bytes to I2C device
 * @param dev_addr Device I2C address (7-bit)
 * @param reg_addr Register address
 * @param data Data to write
 * @param len Number of bytes to write
 * @return 0 on success, -1 on error
 */
int i2c_bus_write(uint8_t dev_addr, uint8_t reg_addr, uint8_t *data, uint8_t len);

#ifdef __cplusplus
}
#endif

#endif // I2C_BUS_H
