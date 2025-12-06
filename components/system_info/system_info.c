/**
 * @file system_info.c
 * @brief System information aggregator implementation
 */

#include "system_info.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

// Include drivers
#include "axp2101_driver.h"
#include "pcf85063_driver.h"
#include "app_wifi.h"

static const char *TAG = "system_info";

// Polling interval in milliseconds
#define POLL_INTERVAL_MS    1000
#define POLL_TASK_STACK     4096
#define POLL_TASK_PRIORITY  5

// State
static bool s_initialized = false;
static system_info_t s_info = {0};
static SemaphoreHandle_t s_mutex = NULL;
static TaskHandle_t s_poll_task = NULL;
static bool s_has_gps = false;

// Forward declarations
static void poll_task(void *arg);
static void update_wifi_info(void);
static void update_battery_info(void);
static void update_memory_info(void);
static void update_rtc_info(void);
static void update_gps_info(void);

esp_err_t system_info_init(void)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "System info already initialized");
        return ESP_OK;
    }

    // Create mutex
    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }

    // Initialize info structure
    memset(&s_info, 0, sizeof(s_info));

    // Check for GPS availability (compile-time or runtime detection)
#ifdef CONFIG_GPS_ENABLED
    s_has_gps = true;
#else
    s_has_gps = false;
#endif
    s_info.gps_available = s_has_gps;

    // Get initial total heap size
    s_info.total_heap = heap_caps_get_total_size(MALLOC_CAP_DEFAULT);

    // Create polling task
    BaseType_t ret = xTaskCreate(poll_task, "sys_info", POLL_TASK_STACK,
                                  NULL, POLL_TASK_PRIORITY, &s_poll_task);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create poll task");
        vSemaphoreDelete(s_mutex);
        s_mutex = NULL;
        return ESP_ERR_NO_MEM;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "System info initialized (GPS: %s)",
             s_has_gps ? "available" : "not available");

    return ESP_OK;
}

esp_err_t system_info_deinit(void)
{
    if (!s_initialized) {
        return ESP_OK;
    }

    // Stop poll task
    if (s_poll_task != NULL) {
        vTaskDelete(s_poll_task);
        s_poll_task = NULL;
    }

    // Delete mutex
    if (s_mutex != NULL) {
        vSemaphoreDelete(s_mutex);
        s_mutex = NULL;
    }

    s_initialized = false;
    ESP_LOGI(TAG, "System info deinitialized");

    return ESP_OK;
}

esp_err_t system_info_get(system_info_t *info)
{
    if (!s_initialized || info == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        memcpy(info, &s_info, sizeof(system_info_t));
        xSemaphoreGive(s_mutex);
        return ESP_OK;
    }

    return ESP_ERR_TIMEOUT;
}

const char* system_info_rssi_to_string(int8_t rssi)
{
    if (rssi == 0) {
        return "No Signal";
    } else if (rssi >= -50) {
        return "Excellent";
    } else if (rssi >= -60) {
        return "Good";
    } else if (rssi >= -70) {
        return "Fair";
    } else {
        return "Weak";
    }
}

const char* system_info_battery_status(uint8_t percent, bool charging)
{
    if (charging) {
        return "Charging";
    } else if (percent >= 95) {
        return "Full";
    } else if (percent >= 60) {
        return "High";
    } else if (percent >= 30) {
        return "Medium";
    } else if (percent >= 10) {
        return "Low";
    } else {
        return "Critical";
    }
}

void system_info_format_uptime(uint32_t uptime_seconds, char *buf, size_t buf_size)
{
    if (buf == NULL || buf_size < 16) {
        return;
    }

    uint32_t days = uptime_seconds / 86400;
    uint32_t hours = (uptime_seconds % 86400) / 3600;
    uint32_t minutes = (uptime_seconds % 3600) / 60;
    uint32_t seconds = uptime_seconds % 60;

    if (days > 0) {
        snprintf(buf, buf_size, "%lud %02lu:%02lu:%02lu",
                 (unsigned long)days, (unsigned long)hours,
                 (unsigned long)minutes, (unsigned long)seconds);
    } else {
        snprintf(buf, buf_size, "%02lu:%02lu:%02lu",
                 (unsigned long)hours, (unsigned long)minutes,
                 (unsigned long)seconds);
    }
}

bool system_info_has_gps(void)
{
    return s_has_gps;
}

// ============================================
// Private Functions
// ============================================

static void poll_task(void *arg)
{
    TickType_t last_wake = xTaskGetTickCount();

    while (1) {
        // Update all info with mutex protection
        if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            update_wifi_info();
            update_battery_info();
            update_memory_info();
            update_rtc_info();
            update_gps_info();

            // Update uptime
            s_info.uptime_seconds = (uint32_t)(esp_timer_get_time() / 1000000);
            s_info.last_update_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;

            xSemaphoreGive(s_mutex);
        }

        // Wait for next poll interval
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(POLL_INTERVAL_MS));
    }
}

static void update_wifi_info(void)
{
    s_info.wifi_connected = app_wifi_is_connected();

    if (s_info.wifi_connected) {
        s_info.wifi_rssi = app_wifi_get_rssi();
        app_wifi_get_ip_string(s_info.wifi_ip, sizeof(s_info.wifi_ip));

        // Get SSID from AP info
        wifi_ap_record_t ap_info;
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            strncpy(s_info.wifi_ssid, (char*)ap_info.ssid, sizeof(s_info.wifi_ssid) - 1);
            s_info.wifi_ssid[sizeof(s_info.wifi_ssid) - 1] = '\0';
        }
    } else {
        s_info.wifi_rssi = 0;
        s_info.wifi_ip[0] = '\0';
        s_info.wifi_ssid[0] = '\0';
    }
}

static void update_battery_info(void)
{
    axp2101_info_t batt_info;
    if (axp2101_get_info(&batt_info) == ESP_OK) {
        s_info.battery_percent = batt_info.percent;
        s_info.battery_voltage_mv = batt_info.voltage_mv;
        s_info.battery_charging = batt_info.is_charging;
        s_info.battery_present = batt_info.is_battery_present;
        s_info.temperature_c = batt_info.temperature_c;
    }
}

static void update_memory_info(void)
{
    s_info.free_heap = heap_caps_get_free_size(MALLOC_CAP_DEFAULT);
    s_info.min_free_heap = heap_caps_get_minimum_free_size(MALLOC_CAP_DEFAULT);

    // Calculate usage percentage
    if (s_info.total_heap > 0) {
        uint32_t used = s_info.total_heap - s_info.free_heap;
        s_info.heap_usage_percent = (uint8_t)((used * 100) / s_info.total_heap);
    }
}

static void update_rtc_info(void)
{
    pcf85063_datetime_t dt;
    if (pcf85063_get_datetime(&dt) == ESP_OK) {
        s_info.rtc_year = dt.year;
        s_info.rtc_month = dt.month;
        s_info.rtc_day = dt.day;
        s_info.rtc_weekday = dt.weekday;
        s_info.rtc_hour = dt.hour;
        s_info.rtc_minute = dt.minute;
        s_info.rtc_second = dt.second;
    }
}

static void update_gps_info(void)
{
    if (!s_has_gps) {
        s_info.gps_fix = false;
        return;
    }

    // TODO: Integrate with GPS driver when available
    // For now, just mark as no fix
    s_info.gps_fix = false;
    s_info.gps_latitude = 0.0;
    s_info.gps_longitude = 0.0;
    s_info.gps_altitude = 0.0f;
    s_info.gps_satellites = 0;
}
