/**
 * @file app_wifi.h
 * @brief WiFi management for the voice assistant
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================
// WiFi Events
// ============================================

/**
 * @brief WiFi event types (prefixed to avoid ESP-IDF conflict)
 */
typedef enum {
    APP_WIFI_EVENT_CONNECTING = 0,
    APP_WIFI_EVENT_CONNECTED,
    APP_WIFI_EVENT_DISCONNECTED,
    APP_WIFI_EVENT_CONNECTION_FAILED,
    APP_WIFI_EVENT_GOT_IP,
} app_wifi_event_t;

/**
 * @brief WiFi event callback function type
 */
typedef void (*app_wifi_event_callback_t)(app_wifi_event_t event);

/**
 * @brief WiFi configuration structure (prefixed to avoid ESP-IDF conflict)
 */
typedef struct {
    const char *ssid;               // WiFi SSID
    const char *password;           // WiFi password
    app_wifi_event_callback_t event_callback;  // Event callback
} app_wifi_config_t;

// ============================================
// WiFi Function Declarations
// ============================================

/**
 * @brief Initialize WiFi
 *
 * @param config WiFi configuration
 * @return ESP_OK on success
 */
esp_err_t app_wifi_init(const app_wifi_config_t *config);

/**
 * @brief Deinitialize WiFi
 *
 * @return ESP_OK on success
 */
esp_err_t app_wifi_deinit(void);

/**
 * @brief Connect to WiFi
 *
 * @return ESP_OK on success
 */
esp_err_t app_wifi_connect(void);

/**
 * @brief Disconnect from WiFi
 *
 * @return ESP_OK on success
 */
esp_err_t app_wifi_disconnect(void);

/**
 * @brief Reconnect to WiFi
 *
 * @return ESP_OK on success
 */
esp_err_t app_wifi_reconnect(void);

/**
 * @brief Check if WiFi is connected
 *
 * @return true if connected
 */
bool app_wifi_is_connected(void);

/**
 * @brief Get WiFi RSSI (signal strength)
 *
 * @return RSSI value in dBm, or 0 if not connected
 */
int8_t app_wifi_get_rssi(void);

/**
 * @brief Get IP address as string
 *
 * @param buffer Output buffer
 * @param size Buffer size
 * @return ESP_OK on success
 */
esp_err_t app_wifi_get_ip_string(char *buffer, size_t size);

#ifdef __cplusplus
}
#endif
