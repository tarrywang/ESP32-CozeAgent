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
#include "webrtc_azure.h"
#include "codec_init.h"  // For get_playback_handle() and get_record_handle() (TDM mode)
#include "ui_manager.h"
#include "app_core.h"
#include "app_wifi.h"
#include "bsp_button.h"
#include "debug_console.h"
#include "media_lib_adapter.h"
#include "media_lib_os.h"

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
// Audio handles are now managed by codec_board (TDM mode for AEC)
// Use get_playback_handle() and get_record_handle() instead

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
 * @brief Azure Realtime event callback (WebSocket - OLD/DISABLED)
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
 * @brief WebRTC Azure event callback (NEW - WebRTC P2P)
 *
 * Handles events from Azure OpenAI Realtime API via WebRTC.
 */
static void webrtc_azure_event_callback(webrtc_azure_event_t *event, void *user_data)
{
    if (!event) {
        return;
    }

    switch (event->type) {
        case WEBRTC_AZURE_EVENT_CONNECTED:
            ESP_LOGI(TAG, "✅ WebRTC connected");
            xEventGroupSetBits(g_system_event_group, AZURE_CONNECTED_BIT);
            break;

        case WEBRTC_AZURE_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "❌ WebRTC disconnected");
            xEventGroupClearBits(g_system_event_group, AZURE_CONNECTED_BIT);
            break;

        case WEBRTC_AZURE_EVENT_DATA_CHANNEL_OPEN:
            ESP_LOGI(TAG, "✅ Data channel connected - voice interaction ready");
            break;

        case WEBRTC_AZURE_EVENT_TRANSCRIPT:
            if (event->transcript.text) {
                ESP_LOGI(TAG, "📝 Transcript: %s", event->transcript.text);
            }
            break;

        case WEBRTC_AZURE_EVENT_FUNCTION_CALL:
            ESP_LOGI(TAG, "🔧 Function call: %s", event->function_call.name);
            break;

        case WEBRTC_AZURE_EVENT_ERROR:
            ESP_LOGE(TAG, "❌ WebRTC error: %s (code: %d)",
                     event->error.message ? event->error.message : "unknown",
                     event->error.code);
            break;

        default:
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
 * @brief Get speaker codec handle (from codec_board TDM mode)
 */
esp_codec_dev_handle_t app_get_speaker_codec(void)
{
    return get_playback_handle();  // From codec_board (TDM mode)
}

/**
 * @brief Get microphone codec handle (from codec_board TDM mode)
 */
esp_codec_dev_handle_t app_get_mic_codec(void)
{
    return get_record_handle();  // From codec_board (TDM mode)
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

    // ==== Phase 4: Audio Initialization (TDM mode for AEC) ====
    ESP_LOGI(TAG, "Initializing audio with TDM mode for AEC...");

    // Initialize audio board using codec_board with TDM mode
    // This provides 4-channel I2S data required for AEC:
    // - Channel 0: MIC input (user voice)
    // - Channel 1: Reference signal (speaker output for echo cancellation)
    init_audio_board();

    // Verify codec handles are available
    esp_codec_dev_handle_t spk_handle = get_playback_handle();
    esp_codec_dev_handle_t mic_handle = get_record_handle();
    if (spk_handle == NULL || mic_handle == NULL) {
        ESP_LOGE(TAG, "Audio codec init failed: spk=%p, mic=%p", spk_handle, mic_handle);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Audio initialized with TDM mode: spk=%p, mic=%p", spk_handle, mic_handle);
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

    // ==== Phase 9: WebRTC Azure Initialization (replaces old WebSocket Azure) ====
    ESP_LOGI(TAG, "Adding media lib adapter...");
    media_lib_add_default_adapter();

    ESP_LOGI(TAG, "Initializing WebRTC Azure module...");
    webrtc_azure_config_t webrtc_cfg = {
        .wifi_ssid = WIFI_SSID,
        .wifi_password = WIFI_PASSWORD,
        .event_cb = webrtc_azure_event_callback,
        .user_data = NULL,
    };

    ret = webrtc_azure_init(&webrtc_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WebRTC Azure init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "WebRTC Azure module initialized");

    // Start WebRTC if WiFi is already connected
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Starting WebRTC connection...");
        ret = webrtc_azure_start();
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "WebRTC start failed (will retry): %s", esp_err_to_name(ret));
            // Not critical - will retry in main loop
        }
    }

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
            ESP_LOGI(TAG, "WebRTC: %s", webrtc_azure_is_connected() ? "CONNECTED" : "DISCONNECTED");
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

        // Auto-connect WebRTC if WiFi is connected but WebRTC is not running
        // Use is_running() instead of is_connected() to avoid interrupting DTLS handshake
        if ((bits & WIFI_CONNECTED_BIT) && !webrtc_azure_is_running()) {
            ESP_LOGI(TAG, "WiFi connected, starting WebRTC connection...");
            log_current_time("WebRTC connect");
            webrtc_azure_start();
        }

        // Query WebRTC status periodically
        webrtc_azure_query();

        // Sleep for 10 seconds
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
