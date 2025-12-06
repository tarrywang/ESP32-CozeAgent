/**
 * @file debug_console.c
 * @brief Debug Console Implementation
 */

#include "debug_console.h"
#include "app_core.h"
#include "app_events.h"
#include "coze_ws.h"
#include "esp_log.h"
#include "esp_console.h"
#include "argtable3/argtable3.h"
#include "driver/uart.h"
#include "linenoise/linenoise.h"
#include <string.h>

static const char *TAG = "DEBUG_CONSOLE";

// ============================================
// Command Implementations
// ============================================

static struct {
    struct arg_str *message;
    struct arg_end *end;
} send_args;

static struct {
    struct arg_end *end;
} start_args;

static struct {
    struct arg_end *end;
} status_args;

/**
 * @brief Send text message command
 */
static int cmd_send(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **) &send_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, send_args.end, argv[0]);
        return 1;
    }

    const char *message = send_args.message->sval[0];

    ESP_LOGI(TAG, "📤 Sending message: '%s'", message);

    // Check if connected
    if (!coze_ws_is_connected()) {
        ESP_LOGW(TAG, "⚠️  Not connected to Coze server!");
        return 1;
    }

    // Send text message
    esp_err_t ret = coze_ws_send_text(message);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ Failed to send message: %s", esp_err_to_name(ret));
        return 1;
    }

    // Request response
    ret = coze_ws_create_response();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ Failed to create response: %s", esp_err_to_name(ret));
        return 1;
    }

    ESP_LOGI(TAG, "✅ Message sent, waiting for AI response...");
    return 0;
}

/**
 * @brief Start conversation command (simulate button press)
 */
static int cmd_start(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **) &start_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, start_args.end, argv[0]);
        return 1;
    }

    ESP_LOGI(TAG, "🎤 Starting conversation (simulating button press)...");

    // Post button press event
    app_event_msg_t event = {
        .type = APP_EVENT_BUTTON_PRESS,
        .timestamp = xTaskGetTickCount() * portTICK_PERIOD_MS,
    };

    esp_err_t ret = app_events_post(&event, pdMS_TO_TICKS(100));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ Failed to post event: %s", esp_err_to_name(ret));
        return 1;
    }

    ESP_LOGI(TAG, "✅ Conversation trigger sent");
    return 0;
}

/**
 * @brief Show system status
 */
static int cmd_status(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **) &status_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, status_args.end, argv[0]);
        return 1;
    }

    ESP_LOGI(TAG, "📊 System Status:");
    ESP_LOGI(TAG, "  App State: %d", app_core_get_state());
    ESP_LOGI(TAG, "  Coze State: %s", coze_ws_state_to_string(coze_ws_get_state()));
    ESP_LOGI(TAG, "  Coze Connected: %s", coze_ws_is_connected() ? "YES" : "NO");

    const char *session_id = coze_ws_get_session_id();
    if (session_id) {
        ESP_LOGI(TAG, "  Session ID: %s", session_id);
    }

    // Memory info
    ESP_LOGI(TAG, "  Free heap: %lu bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "  Free internal: %lu bytes", heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    ESP_LOGI(TAG, "  Free PSRAM: %lu bytes", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    return 0;
}

/**
 * @brief Send quick test message "你好"
 */
static int cmd_hello(int argc, char **argv)
{
    ESP_LOGI(TAG, "📤 Sending test message: '你好'");

    if (!coze_ws_is_connected()) {
        ESP_LOGW(TAG, "⚠️  Not connected to Coze server!");
        return 1;
    }

    esp_err_t ret = coze_ws_send_text("你好");
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ Failed to send: %s", esp_err_to_name(ret));
        return 1;
    }

    ret = coze_ws_create_response();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ Failed to create response: %s", esp_err_to_name(ret));
        return 1;
    }

    ESP_LOGI(TAG, "✅ '你好' sent, waiting for AI response...");
    return 0;
}

// ============================================
// Console Initialization
// ============================================

/**
 * @brief Register console commands
 */
static void register_commands(void)
{
    // Register 'send' command
    send_args.message = arg_str1(NULL, NULL, "<message>", "Text message to send");
    send_args.end = arg_end(2);

    const esp_console_cmd_t send_cmd = {
        .command = "send",
        .help = "Send text message to Coze bot",
        .hint = NULL,
        .func = &cmd_send,
        .argtable = &send_args
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&send_cmd));

    // Register 'start' command
    start_args.end = arg_end(1);

    const esp_console_cmd_t start_cmd = {
        .command = "start",
        .help = "Start conversation (simulate button press)",
        .hint = NULL,
        .func = &cmd_start,
        .argtable = &start_args
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&start_cmd));

    // Register 'status' command
    status_args.end = arg_end(1);

    const esp_console_cmd_t status_cmd = {
        .command = "status",
        .help = "Show system status",
        .hint = NULL,
        .func = &cmd_status,
        .argtable = &status_args
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&status_cmd));

    // Register 'hello' command (quick test)
    const esp_console_cmd_t hello_cmd = {
        .command = "hello",
        .help = "Send '你好' to Coze bot (quick test)",
        .hint = NULL,
        .func = &cmd_hello,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&hello_cmd));

    ESP_LOGI(TAG, "Registered commands: send, start, status, hello");
}

esp_err_t debug_console_init(void)
{
    ESP_LOGI(TAG, "Initializing debug console...");

    // Initialize console REPL environment
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_config.prompt = "coze> ";
    repl_config.max_cmdline_length = 256;

    // Initialize UART for console
    esp_console_dev_uart_config_t uart_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_uart(&uart_config, &repl_config, &repl));

    // Register commands
    register_commands();

    // Print welcome message
    printf("\n");
    printf("===========================================\n");
    printf("  ESP32-S3 Coze Voice Agent Debug Console\n");
    printf("===========================================\n");
    printf("Commands:\n");
    printf("  send <message>  - Send text to Coze bot\n");
    printf("  hello           - Send '你好' (quick test)\n");
    printf("  start           - Start conversation\n");
    printf("  status          - Show system status\n");
    printf("  help            - Show all commands\n");
    printf("===========================================\n\n");

    // Start REPL
    ESP_ERROR_CHECK(esp_console_start_repl(repl));

    ESP_LOGI(TAG, "Debug console initialized");
    return ESP_OK;
}

esp_err_t debug_console_send_message(const char *message)
{
    if (!message) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Sending message: '%s'", message);

    if (!coze_ws_is_connected()) {
        ESP_LOGW(TAG, "Not connected to Coze");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = coze_ws_send_text(message);
    if (ret != ESP_OK) {
        return ret;
    }

    return coze_ws_create_response();
}

esp_err_t debug_console_trigger_conversation(void)
{
    ESP_LOGI(TAG, "Triggering conversation");

    app_event_msg_t event = {
        .type = APP_EVENT_BUTTON_PRESS,
        .timestamp = xTaskGetTickCount() * portTICK_PERIOD_MS,
    };

    return app_events_post(&event, pdMS_TO_TICKS(100));
}
