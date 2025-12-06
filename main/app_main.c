/**
 * @file app_main.c
 * @brief Main application entry point for ESP32-S3 Coze Voice Agent
 *
 * This file initializes all system components and starts the main application tasks:
 * - Waveshare BSP (Display, Touch, Audio)
 * - WiFi connection
 * - Audio pipeline (recording and playback)
 * - Coze WebSocket client
 * - LVGL UI
 * - Application core state machine
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_check.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include <time.h>

// Official Waveshare BSP
#include "bsp/esp-bsp.h"

// Component includes
#include "audio_pipeline.h"
#include "coze_ws.h"
#include "azure_realtime.h"
#include "ui_manager.h"
#include "app_core.h"
#include "app_wifi.h"
#include "bsp_button.h"
#include "debug_console.h"

// Sensor drivers and system info
#include "axp2101_driver.h"
#include "pcf85063_driver.h"
#include "system_info.h"

// Manual display initialization (bypasses BSP display to fix SPI queue issue)
#include "display_init.h"

static const char *TAG = "APP_MAIN";

// ============================================
// WiFi Configuration (Hardcoded for simplicity)
// In production, use menuconfig or provisioning
// ============================================
#define WIFI_SSID      "TarryWang"
#define WIFI_PASSWORD  "Wat10sons"

// ============================================
// System Event Group
// ============================================
EventGroupHandle_t g_system_event_group = NULL;

// BSP handles
static lv_display_t *s_lv_display = NULL;
static lv_indev_t *s_lv_indev = NULL;
static esp_codec_dev_handle_t s_spk_codec = NULL;
static esp_codec_dev_handle_t s_mic_codec = NULL;

static void log_current_time(const char *prefix)
{
    time_t now = 0;
    time(&now);
    struct tm timeinfo = {0};
    localtime_r(&now, &timeinfo);
    ESP_LOGI(TAG, "%s time: unix=%lld, %04d-%02d-%02d %02d:%02d:%02d",
             prefix, (long long)now,
             timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
             timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
}

// Event bits
#define WIFI_CONNECTED_BIT     BIT0
#define WIFI_FAIL_BIT          BIT1
#define COZE_CONNECTED_BIT     BIT2  // Keep for backward compatibility
#define AZURE_CONNECTED_BIT    BIT2  // Azure replaces Coze
#define AUDIO_READY_BIT        BIT3
#define UI_READY_BIT           BIT4

/**
 * @brief Initialize NVS flash storage
 */
static esp_err_t init_nvs(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition needs erase");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    return ret;
}

/**
 * @brief Initialize system event loop and netif
 */
static esp_err_t init_event_system(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Create system event group
    g_system_event_group = xEventGroupCreate();
    if (g_system_event_group == NULL) {
        ESP_LOGE(TAG, "Failed to create event group");
        return ESP_FAIL;
    }

    return ESP_OK;
}

/**
 * @brief Button event callback - posts button press events to app_core
 */
static void button_event_callback(bsp_button_event_t event, void *user_data)
{
    switch (event) {
        case BSP_BUTTON_EVENT_SHORT_CLICK:
            ESP_LOGI(TAG, "🔘 BOOT button SHORT click");
            app_core_send_event(APP_EVENT_BUTTON_PRESS);
            break;

        case BSP_BUTTON_EVENT_LONG_PRESS:
            ESP_LOGI(TAG, "🔘 BOOT button LONG press");
            app_core_send_event(APP_EVENT_USER_LONG_PRESS);
            break;

        case BSP_BUTTON_EVENT_PRESSED:
            ESP_LOGD(TAG, "🔘 BOOT button pressed");
            break;

        case BSP_BUTTON_EVENT_RELEASED:
            ESP_LOGD(TAG, "🔘 BOOT button released");
            break;

        default:
            break;
    }
}

/**
 * @brief WiFi connection callback - called when WiFi connects/disconnects
 */
static void wifi_event_callback(app_wifi_event_t event)
{
    switch (event) {
        case APP_WIFI_EVENT_CONNECTED:
            ESP_LOGI(TAG, "✅ WiFi CONNECTED");
            xEventGroupSetBits(g_system_event_group, WIFI_CONNECTED_BIT);
            xEventGroupClearBits(g_system_event_group, WIFI_FAIL_BIT);
            break;

        case APP_WIFI_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "❌ WiFi DISCONNECTED");
            xEventGroupClearBits(g_system_event_group, WIFI_CONNECTED_BIT);
            xEventGroupSetBits(g_system_event_group, WIFI_FAIL_BIT);
            break;

        case APP_WIFI_EVENT_CONNECTING:
            ESP_LOGI(TAG, "🔄 WiFi connecting...");
            break;

        default:
            break;
    }
}

/**
 * @brief Azure Realtime event callback
 *
 * Handles events from Azure OpenAI Realtime API including session creation,
 * audio responses, transcripts, and errors.
 */
static void azure_realtime_event_callback(const azure_event_t *event, void *user_data)
{
    if (!event) {
        return;
    }

    switch (event->type) {
        case AZURE_MSG_TYPE_SESSION_CREATED:
            ESP_LOGI(TAG, "✅ Azure session created: %s", event->session_id);
            break;

        case AZURE_MSG_TYPE_RESPONSE_AUDIO_DELTA:
            ESP_LOGD(TAG, "🔊 Received audio chunk: %zu bytes", event->audio_size);
            // TODO: Forward audio to speaker via audio pipeline
            break;

        case AZURE_MSG_TYPE_RESPONSE_AUDIO_TRANSCRIPT_DELTA:
            if (event->text) {
                ESP_LOGI(TAG, "📝 Transcript: %s", event->text);
            }
            break;

        case AZURE_MSG_TYPE_RESPONSE_DONE:
            ESP_LOGI(TAG, "✅ Response complete");
            break;

        case AZURE_MSG_TYPE_ERROR:
            ESP_LOGE(TAG, "❌ Azure error: %s (code: %d)",
                     event->error_message ? event->error_message : "unknown",
                     event->error_code);
            break;

        default:
            ESP_LOGD(TAG, "Azure event: %s",
                     azure_realtime_msg_type_to_string(event->type));
            break;
    }
}

/**
 * @brief Get the LVGL display handle
 */
lv_display_t *app_get_display(void)
{
    return s_lv_display;
}

/**
 * @brief Get the LVGL input device handle
 */
lv_indev_t *app_get_input_dev(void)
{
    return s_lv_indev;
}

/**
 * @brief Get speaker codec handle
 */
esp_codec_dev_handle_t app_get_speaker_codec(void)
{
    return s_spk_codec;
}

/**
 * @brief Get microphone codec handle
 */
esp_codec_dev_handle_t app_get_mic_codec(void)
{
    return s_mic_codec;
}

/**
 * @brief Sync system time via SNTP before starting TLS connections
 */
static esp_err_t sync_system_time(void)
{
    ESP_LOGI(TAG, "Syncing time via SNTP...");
    log_current_time("SNTP start");

    // Use a reachable NTP server; adjust if your network屏蔽外网
    const esp_sntp_config_t sntp_cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("ntp.aliyun.com");

    // Clean any previous SNTP state, ignore result if it wasn't initialized
    esp_netif_sntp_deinit();

    ESP_RETURN_ON_ERROR(esp_netif_sntp_init(&sntp_cfg), TAG, "SNTP init failed");
    ESP_RETURN_ON_ERROR(esp_netif_sntp_start(), TAG, "SNTP start failed");

    // Wait up to 10s for first sync
    esp_err_t err = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(10000));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SNTP sync timeout/failed: %s", esp_err_to_name(err));
        return err;
    }

    time_t now = 0;
    time(&now);
    if (now < 1700000000) { // ~2023-11-14, guard against 1970/2000 dates
        ESP_LOGE(TAG, "Time still invalid after SNTP");
        return ESP_FAIL;
    }

    struct tm timeinfo = {0};
    localtime_r(&now, &timeinfo);
    ESP_LOGI(TAG, "Time synced: %04d-%02d-%02d %02d:%02d:%02d",
             timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
             timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    log_current_time("SNTP done");
    return ESP_OK;
}

/**
 * @brief Initialize all system components in proper order
 */
static esp_err_t system_init(void)
{
    esp_err_t ret;

    // ==== Phase 1: Core System Initialization ====
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "ESP32-S3 Coze Voice Agent Starting...");
    ESP_LOGI(TAG, "========================================");

    // Initialize NVS
    ret = init_nvs();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "NVS initialized");

    // Initialize event system
    ret = init_event_system();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Event system init failed");
        return ret;
    }
    ESP_LOGI(TAG, "Event system initialized");

    // ==== Phase 2: I2C Bus Initialization ====
    // I2C is initialized lazily by drivers via bsp_i2c_init() (new driver API)
    ESP_LOGI(TAG, "I2C bus will be initialized by drivers (new API)");

    // ==== Phase 2.1: PMU and RTC Initialization ====
    ESP_LOGI(TAG, "Initializing AXP2101 PMU...");
    ret = axp2101_init(0, 15, 14);  // I2C_NUM_0, SDA=GPIO15, SCL=GPIO14
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "AXP2101 init failed (non-critical): %s", esp_err_to_name(ret));
        // Not critical, continue
    } else {
        ESP_LOGI(TAG, "AXP2101 PMU initialized (battery: %d%%)", axp2101_get_battery_percent());
    }

    ESP_LOGI(TAG, "Initializing PCF85063 RTC...");
    ret = pcf85063_init(0, 15, 14);  // I2C_NUM_0, SDA=GPIO15, SCL=GPIO14
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "PCF85063 init failed (non-critical): %s", esp_err_to_name(ret));
        // Not critical, continue
    } else {
        char time_str[16];
        pcf85063_get_time_str(time_str, sizeof(time_str));
        ESP_LOGI(TAG, "PCF85063 RTC initialized (time: %s)", time_str);
    }

    // Initialize system info aggregator
    ret = system_info_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "System info init failed (non-critical): %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "System info aggregator initialized");
    }

    // ==== Phase 3: Display + LVGL Initialization ====
    // Using manual display initialization (bypasses BSP to fix SPI queue issue)
    ESP_LOGI(TAG, "Initializing display + LVGL (manual init)...");
    ret = display_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Display init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    s_lv_display = display_get_lv_disp();
    if (s_lv_display == NULL) {
        ESP_LOGE(TAG, "Display handle is NULL after init");
        return ESP_FAIL;
    }
    // Touch is not initialized yet (requires separate implementation)
    s_lv_indev = NULL;
    ESP_LOGW(TAG, "Touch input not available (manual display init)");
    ESP_LOGI(TAG, "Display + LVGL initialized (manual)");

    // ==== Phase 4: Audio Initialization ====
    ESP_LOGI(TAG, "Initializing audio...");
    ret = bsp_audio_init(NULL);  // Use default I2S config
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Audio init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Initialize speaker codec
    s_spk_codec = bsp_audio_codec_speaker_init();
    if (s_spk_codec == NULL) {
        ESP_LOGE(TAG, "Speaker codec init failed");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Speaker codec initialized");

    // Initialize microphone codec
    s_mic_codec = bsp_audio_codec_microphone_init();
    if (s_mic_codec == NULL) {
        ESP_LOGE(TAG, "Microphone codec init failed");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Microphone codec initialized");
    xEventGroupSetBits(g_system_event_group, AUDIO_READY_BIT);

    // ==== Phase 5: UI Manager Initialization ====
    ESP_LOGI(TAG, "Initializing UI manager...");
    ret = ui_manager_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "UI manager init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    xEventGroupSetBits(g_system_event_group, UI_READY_BIT);

    // ==== Phase 6: WiFi Initialization ====
    ESP_LOGI(TAG, "Initializing WiFi...");

    app_wifi_config_t wifi_cfg = {
        .ssid = WIFI_SSID,
        .password = WIFI_PASSWORD,
        .event_callback = wifi_event_callback,
    };

    ret = app_wifi_init(&wifi_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Wait for WiFi connection (with timeout)
    EventBits_t bits = xEventGroupWaitBits(g_system_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE, pdFALSE,
                                           pdMS_TO_TICKS(30000));

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "WiFi connected successfully");
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGW(TAG, "WiFi connection failed, will retry in background");
    } else {
        ESP_LOGW(TAG, "WiFi connection timeout");
    }

    log_current_time("Before SNTP");

    // ==== Phase 7: SNTP Time Sync ====
    if (bits & WIFI_CONNECTED_BIT) {
        ret = sync_system_time();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Time sync failed; aborting to avoid TLS issues");
            return ret;
        }
        log_current_time("After SNTP");
    } else {
        ESP_LOGW(TAG, "Skipping time sync (WiFi not connected)");
    }

    // ==== Phase 8: Audio Pipeline Initialization ====
    ESP_LOGI(TAG, "Initializing audio pipeline...");
    ret = audio_pipeline_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Audio pipeline init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "Audio pipeline initialized");

    // ==== Phase 9: Azure Realtime API Initialization ====
    // DISABLED: Temporarily disable Azure for display testing
#if 0
    ESP_LOGI(TAG, "Initializing Azure Realtime client...");
    ret = azure_realtime_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Azure Realtime init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Configure Azure OpenAI credentials
    azure_realtime_config_t azure_cfg = {
        .api_key = "2d621e68de6c4c1eb24e3f686c4b54df",
        .resource_name = "anony-company",  // Azure resource name (SDK appends .openai.azure.com)
        .deployment_name = "gpt-realtime",
        .voice = "alloy",
        .sample_rate = 8000,
        .audio_format = "g711_ulaw",
        .use_server_vad = false,
        .callback = azure_realtime_event_callback,  // Event callback handler
        .user_data = NULL,
    };

    ret = azure_realtime_configure(&azure_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Azure configure failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "Azure Realtime client configured (resource: %s, deployment: %s)",
             azure_cfg.resource_name, azure_cfg.deployment_name);
#endif
    ESP_LOGW(TAG, "Azure Realtime DISABLED for display testing");

    // ==== Phase 9: Application Core Initialization ====
    ESP_LOGI(TAG, "Initializing application core...");
    ret = app_core_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "App core init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "Application core initialized");

    // ==== Phase 10: Button Initialization ====
    ESP_LOGI(TAG, "Initializing BOOT button...");
    ret = bsp_button_init(button_event_callback, NULL);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Button init failed (non-critical): %s", esp_err_to_name(ret));
        // Not critical, continue
    } else {
        ESP_LOGI(TAG, "BOOT button initialized (GPIO 0)");
    }

    // ==== Phase 11: Debug Console Initialization ====
    ESP_LOGI(TAG, "Initializing debug console...");
    ret = debug_console_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Debug console init failed (non-critical): %s", esp_err_to_name(ret));
        // Not critical, continue
    } else {
        ESP_LOGI(TAG, "Debug console initialized");
    }

    return ESP_OK;
}

/**
 * @brief Start all application tasks
 */
static esp_err_t start_application_tasks(void)
{
    ESP_LOGI(TAG, "Starting application tasks...");

    // Start UI task (handles LVGL refresh and touch events)
    esp_err_t ret = ui_manager_start_task();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start UI task: %s", esp_err_to_name(ret));
        return ret;
    }

    // Start audio tasks (recording and playback)
    ret = audio_pipeline_start_tasks();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start audio tasks");
        return ret;
    }

    // Start Azure Realtime task
    // DISABLED: Azure task for display testing
#if 0
    ret = azure_realtime_start_task();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start Azure Realtime task");
        return ret;
    }
#endif

    // Start application core task (state machine)
    ret = app_core_start_task();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start app core task");
        return ret;
    }

    ESP_LOGI(TAG, "All application tasks started");
    return ESP_OK;
}

/**
 * @brief Main application entry point
 */
void app_main(void)
{
    // Print system information
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Chip: ESP32-S3");
    ESP_LOGI(TAG, "Free heap: %lu bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "IDF version: %s", esp_get_idf_version());
    ESP_LOGI(TAG, "========================================");

    // Initialize all components
    esp_err_t ret = system_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ System initialization FAILED!");
        ESP_LOGE(TAG, "Entering error state...");
        while (1) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    // Start all application tasks
    ret = start_application_tasks();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ Failed to start application tasks!");
        while (1) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    // Transition to idle state
    app_core_set_state(APP_STATE_IDLE);

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "✅ System READY! Voice interaction mode");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "📍 How to start conversation:");
    ESP_LOGI(TAG, "  1. 🔘 Press BOOT button (GPIO 0)");
    ESP_LOGI(TAG, "  2. 💻 Serial: 'start' or 'hello'");
    ESP_LOGI(TAG, "  3. 🛑 Long press BOOT = cancel");
    ESP_LOGI(TAG, "========================================");

    // Main loop - monitor system health and show status
    static int log_counter = 0;
    while (1) {
        EventBits_t bits = xEventGroupGetBits(g_system_event_group);

        // Log detailed status every 5 iterations (50 seconds)
        if (log_counter % 5 == 0) {
            ESP_LOGI(TAG, "--- STATUS ---");
            ESP_LOGI(TAG, "WiFi: %s", (bits & WIFI_CONNECTED_BIT) ? "CONNECTED" : "DISCONNECTED");
            // DISABLED: Azure status for display testing
            // ESP_LOGI(TAG, "Azure: %s", azure_realtime_state_to_string(azure_realtime_get_state()));
            ESP_LOGI(TAG, "Azure: DISABLED (display testing)");
            ESP_LOGI(TAG, "App:  %s", app_core_state_to_string(app_core_get_state()));
            ESP_LOGI(TAG, "Ready for conversation: %s", app_core_is_ready() ? "YES - TAP TO START" : "NO");
            ESP_LOGI(TAG, "Free heap: %lu bytes", esp_get_free_heap_size());
            ESP_LOGI(TAG, "--------------");
        }
        log_counter++;

        // Check for WiFi reconnection if needed
        if (!(bits & WIFI_CONNECTED_BIT)) {
            ESP_LOGW(TAG, "WiFi not connected, attempting reconnection...");
            app_wifi_reconnect();
        }

        // Auto-connect to Azure if WiFi is connected but Azure is disconnected
        // DISABLED: Azure auto-reconnect for display testing
#if 0
        if ((bits & WIFI_CONNECTED_BIT) && azure_realtime_get_state() == AZURE_STATE_DISCONNECTED) {
            ESP_LOGI(TAG, "WiFi connected, connecting to Azure server...");
            log_current_time("Azure connect");
            azure_realtime_connect();
        }
#endif

        // Sleep for 10 seconds
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
