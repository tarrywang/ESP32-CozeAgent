/**
 * @file app_wifi.c
 * @brief WiFi management implementation
 */

#include "app_wifi.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"

static const char *TAG = "APP_WIFI";

// ============================================
// Private Variables
// ============================================

static bool s_initialized = false;
static bool s_connected = false;
static app_wifi_config_t s_config;
static esp_netif_t *s_sta_netif = NULL;
static EventGroupHandle_t s_wifi_event_group = NULL;

// Event bits
#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_FAIL_BIT       BIT1

// Retry counter
static int s_retry_num = 0;
#define WIFI_MAX_RETRY      5

// ============================================
// Private Functions
// ============================================

/**
 * @brief WiFi event handler
 */
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                                int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START:
                ESP_LOGI(TAG, "WiFi started, connecting...");
                if (s_config.event_callback) {
                    s_config.event_callback(APP_WIFI_EVENT_CONNECTING);
                }
                esp_wifi_connect();
                break;

            case WIFI_EVENT_STA_DISCONNECTED:
                ESP_LOGI(TAG, "WiFi disconnected");
                s_connected = false;
                if (s_config.event_callback) {
                    s_config.event_callback(APP_WIFI_EVENT_DISCONNECTED);
                }

                if (s_retry_num < WIFI_MAX_RETRY) {
                    s_retry_num++;
                    ESP_LOGI(TAG, "Retrying connection (attempt %d/%d)",
                             s_retry_num, WIFI_MAX_RETRY);
                    vTaskDelay(pdMS_TO_TICKS(1000));
                    esp_wifi_connect();
                } else {
                    xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
                    if (s_config.event_callback) {
                        s_config.event_callback(APP_WIFI_EVENT_CONNECTION_FAILED);
                    }
                }
                break;

            case WIFI_EVENT_STA_CONNECTED:
                ESP_LOGI(TAG, "WiFi connected to AP");
                s_retry_num = 0;
                break;

            default:
                break;
        }
    } else if (event_base == IP_EVENT) {
        switch (event_id) {
            case IP_EVENT_STA_GOT_IP: {
                ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
                ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
                s_connected = true;
                s_retry_num = 0;
                xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
                if (s_config.event_callback) {
                    s_config.event_callback(APP_WIFI_EVENT_CONNECTED);
                    s_config.event_callback(APP_WIFI_EVENT_GOT_IP);
                }
                break;
            }

            default:
                break;
        }
    }
}

// ============================================
// Public Functions
// ============================================

esp_err_t app_wifi_init(const app_wifi_config_t *config)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "WiFi already initialized");
        return ESP_OK;
    }

    if (config == NULL) {
        ESP_LOGE(TAG, "WiFi config is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    s_config = *config;

    ESP_LOGI(TAG, "Initializing WiFi...");

    // Create event group
    s_wifi_event_group = xEventGroupCreate();
    if (s_wifi_event_group == NULL) {
        ESP_LOGE(TAG, "Failed to create WiFi event group");
        return ESP_ERR_NO_MEM;
    }

    // Create default station interface
    s_sta_netif = esp_netif_create_default_wifi_sta();
    if (s_sta_netif == NULL) {
        ESP_LOGE(TAG, "Failed to create WiFi netif");
        return ESP_FAIL;
    }

    // Initialize WiFi with default config
    wifi_init_config_t wifi_init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t ret = esp_wifi_init(&wifi_init_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Register event handlers
    ret = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                               &wifi_event_handler, NULL, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register WiFi event handler");
        return ret;
    }

    ret = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                               &wifi_event_handler, NULL, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register IP event handler");
        return ret;
    }

    // Configure WiFi
    wifi_config_t wifi_cfg = {0};
    strncpy((char *)wifi_cfg.sta.ssid, s_config.ssid, sizeof(wifi_cfg.sta.ssid) - 1);
    strncpy((char *)wifi_cfg.sta.password, s_config.password, sizeof(wifi_cfg.sta.password) - 1);
    wifi_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_cfg.sta.pmf_cfg.capable = true;
    wifi_cfg.sta.pmf_cfg.required = false;

    ret = esp_wifi_set_mode(WIFI_MODE_STA);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set WiFi mode: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set WiFi config: %s", esp_err_to_name(ret));
        return ret;
    }

    // Start WiFi
    ret = esp_wifi_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start WiFi: %s", esp_err_to_name(ret));
        return ret;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "WiFi initialized, connecting to %s...", s_config.ssid);

    return ESP_OK;
}

esp_err_t app_wifi_deinit(void)
{
    if (!s_initialized) {
        return ESP_OK;
    }

    esp_wifi_disconnect();
    esp_wifi_stop();
    esp_wifi_deinit();

    if (s_sta_netif) {
        esp_netif_destroy(s_sta_netif);
        s_sta_netif = NULL;
    }

    if (s_wifi_event_group) {
        vEventGroupDelete(s_wifi_event_group);
        s_wifi_event_group = NULL;
    }

    s_initialized = false;
    s_connected = false;

    ESP_LOGI(TAG, "WiFi deinitialized");
    return ESP_OK;
}

esp_err_t app_wifi_connect(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    s_retry_num = 0;
    return esp_wifi_connect();
}

esp_err_t app_wifi_disconnect(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    return esp_wifi_disconnect();
}

esp_err_t app_wifi_reconnect(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_wifi_disconnect();
    vTaskDelay(pdMS_TO_TICKS(500));
    s_retry_num = 0;
    return esp_wifi_connect();
}

bool app_wifi_is_connected(void)
{
    return s_connected;
}

int8_t app_wifi_get_rssi(void)
{
    if (!s_connected) {
        return 0;
    }

    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        return ap_info.rssi;
    }

    return 0;
}

esp_err_t app_wifi_get_ip_string(char *buffer, size_t size)
{
    if (!s_connected || buffer == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_netif_ip_info_t ip_info;
    esp_err_t ret = esp_netif_get_ip_info(s_sta_netif, &ip_info);
    if (ret != ESP_OK) {
        return ret;
    }

    snprintf(buffer, size, IPSTR, IP2STR(&ip_info.ip));
    return ESP_OK;
}
