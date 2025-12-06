/**
 * @file axp2101_driver.cpp
 * @brief AXP2101 PMU driver implementation using XPowersLib
 */

#include "axp2101_driver.h"
#include "i2c_bus.h"
#include "esp_log.h"

#define XPOWERS_CHIP_AXP2101
#include "XPowersLib.h"

static const char *TAG = "axp2101";

static XPowersPMU s_pmu;
static bool s_initialized = false;

// I2C callback functions for XPowersLib
static int pmu_register_read(uint8_t devAddr, uint8_t regAddr, uint8_t *data, uint8_t len)
{
    return i2c_bus_read(devAddr, regAddr, data, len);
}

static int pmu_register_write(uint8_t devAddr, uint8_t regAddr, uint8_t *data, uint8_t len)
{
    return i2c_bus_write(devAddr, regAddr, data, len);
}

extern "C" {

esp_err_t axp2101_init(int i2c_port, int sda_pin, int scl_pin)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "AXP2101 already initialized");
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

    // Initialize PMU
    if (!s_pmu.begin(AXP2101_SLAVE_ADDRESS, pmu_register_read, pmu_register_write)) {
        ESP_LOGE(TAG, "Failed to initialize AXP2101");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "AXP2101 initialized successfully");

    // Enable measurements
    s_pmu.enableVbusVoltageMeasure();
    s_pmu.enableBattVoltageMeasure();
    s_pmu.enableSystemVoltageMeasure();
    s_pmu.enableTemperatureMeasure();

    // Disable TS pin measurement (no battery temperature sensor)
    s_pmu.disableTSPinMeasure();

    // Set charging parameters
    s_pmu.setPrechargeCurr(XPOWERS_AXP2101_PRECHARGE_50MA);
    s_pmu.setChargerConstantCurr(XPOWERS_AXP2101_CHG_CUR_200MA);
    s_pmu.setChargerTerminationCurr(XPOWERS_AXP2101_CHG_ITERM_25MA);
    s_pmu.setChargeTargetVoltage(XPOWERS_AXP2101_CHG_VOL_4V1);

    // Clear any pending interrupts
    s_pmu.clearIrqStatus();

    s_initialized = true;

    // Log initial status
    ESP_LOGI(TAG, "Battery: %d%%, Voltage: %dmV, Charging: %s",
             s_pmu.getBatteryPercent(),
             s_pmu.getBattVoltage(),
             s_pmu.isCharging() ? "Yes" : "No");

    return ESP_OK;
}

int axp2101_get_battery_percent(void)
{
    if (!s_initialized) {
        return -1;
    }
    return s_pmu.getBatteryPercent();
}

uint16_t axp2101_get_battery_voltage(void)
{
    if (!s_initialized) {
        return 0;
    }
    return s_pmu.getBattVoltage();
}

bool axp2101_is_charging(void)
{
    if (!s_initialized) {
        return false;
    }
    return s_pmu.isCharging();
}

bool axp2101_is_battery_present(void)
{
    if (!s_initialized) {
        return false;
    }
    return s_pmu.isBatteryConnect();
}

float axp2101_get_temperature(void)
{
    if (!s_initialized) {
        return 0.0f;
    }
    return s_pmu.getTemperature();
}

uint16_t axp2101_get_vbus_voltage(void)
{
    if (!s_initialized) {
        return 0;
    }
    return s_pmu.getVbusVoltage();
}

esp_err_t axp2101_get_info(axp2101_info_t *info)
{
    if (!s_initialized || info == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    info->percent = s_pmu.getBatteryPercent();
    info->voltage_mv = s_pmu.getBattVoltage();
    info->is_charging = s_pmu.isCharging();
    info->is_battery_present = s_pmu.isBatteryConnect();
    info->temperature_c = s_pmu.getTemperature();
    info->vbus_voltage_mv = s_pmu.getVbusVoltage();
    info->sys_voltage_mv = s_pmu.getSystemVoltage();

    return ESP_OK;
}

} // extern "C"
