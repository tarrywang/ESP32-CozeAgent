/**
 * @file system_info.h
 * @brief System information aggregator for carousel display
 *
 * Collects WiFi, battery, memory, temperature, GPS, and RTC data
 * in a background task with thread-safe access.
 */

#ifndef SYSTEM_INFO_H
#define SYSTEM_INFO_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief System information data structure
 */
typedef struct {
    // WiFi info
    bool     wifi_connected;
    int8_t   wifi_rssi;           // dBm, 0 if not connected
    char     wifi_ssid[33];       // Current SSID
    char     wifi_ip[16];         // IP address string

    // Battery info (AXP2101)
    uint8_t  battery_percent;     // 0-100
    uint16_t battery_voltage_mv;  // millivolts
    bool     battery_charging;
    bool     battery_present;

    // Memory info
    uint32_t free_heap;           // bytes
    uint32_t min_free_heap;       // minimum since boot
    uint32_t total_heap;          // total heap size
    uint8_t  heap_usage_percent;  // 0-100

    // Temperature (AXP2101 internal)
    float    temperature_c;

    // GPS info (optional)
    bool     gps_available;       // GPS module present
    bool     gps_fix;             // Has valid fix
    double   gps_latitude;
    double   gps_longitude;
    float    gps_altitude;        // meters
    uint8_t  gps_satellites;      // visible satellites

    // RTC info (PCF85063)
    uint16_t rtc_year;
    uint8_t  rtc_month;
    uint8_t  rtc_day;
    uint8_t  rtc_weekday;         // 0=Sunday
    uint8_t  rtc_hour;
    uint8_t  rtc_minute;
    uint8_t  rtc_second;

    // System info
    uint32_t uptime_seconds;      // seconds since boot

    // Update timestamp
    uint32_t last_update_ms;      // tick when last updated
} system_info_t;

/**
 * @brief Initialize system info module
 *
 * Starts background polling task that updates all system info
 * every 1 second.
 *
 * @return ESP_OK on success
 */
esp_err_t system_info_init(void);

/**
 * @brief Deinitialize system info module
 *
 * Stops the background polling task.
 *
 * @return ESP_OK on success
 */
esp_err_t system_info_deinit(void);

/**
 * @brief Get current system information (thread-safe)
 *
 * Copies the latest system info snapshot to the provided structure.
 *
 * @param info Pointer to structure to fill
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if not initialized
 */
esp_err_t system_info_get(system_info_t *info);

/**
 * @brief Get WiFi signal strength description
 *
 * @param rssi RSSI value in dBm
 * @return String description ("Excellent", "Good", "Fair", "Weak", "No Signal")
 */
const char* system_info_rssi_to_string(int8_t rssi);

/**
 * @brief Get battery status description
 *
 * @param percent Battery percentage
 * @param charging Is battery charging
 * @return String description ("Charging", "Full", "High", "Medium", "Low", "Critical")
 */
const char* system_info_battery_status(uint8_t percent, bool charging);

/**
 * @brief Format uptime as human-readable string
 *
 * @param uptime_seconds Total seconds
 * @param buf Output buffer
 * @param buf_size Buffer size (minimum 16 bytes)
 */
void system_info_format_uptime(uint32_t uptime_seconds, char *buf, size_t buf_size);

/**
 * @brief Check if GPS is available
 *
 * @return true if GPS module is present and initialized
 */
bool system_info_has_gps(void);

#ifdef __cplusplus
}
#endif

#endif // SYSTEM_INFO_H
