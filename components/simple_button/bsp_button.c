/**
 * @file bsp_button.c
 * @brief BOOT Button Support Implementation
 */

#include "bsp_button.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"

static const char *TAG = "BSP_BUTTON";

#define BSP_BUTTON_GPIO        GPIO_NUM_0    // BOOT button on GPIO 0
#define BSP_BUTTON_ACTIVE_LOW  1             // BOOT button is active low
#define BSP_LONG_PRESS_MS      1000          // Long press threshold

typedef struct {
    bsp_button_callback_t callback;
    void *user_data;
    uint32_t press_time_ms;
    bool is_pressed;
    TimerHandle_t long_press_timer;
} button_context_t;

static button_context_t s_button_ctx = {0};

static void long_press_timer_callback(TimerHandle_t timer)
{
    if (s_button_ctx.is_pressed && s_button_ctx.callback) {
        ESP_LOGI(TAG, "Long press detected");
        s_button_ctx.callback(BSP_BUTTON_EVENT_LONG_PRESS, s_button_ctx.user_data);
    }
}

static void button_task(void *arg)
{
    bool last_state = false;
    uint32_t press_start_ms = 0;

    while (1) {
        // Read button state
        int level = gpio_get_level(BSP_BUTTON_GPIO);
        bool pressed = (BSP_BUTTON_ACTIVE_LOW ? (level == 0) : (level == 1));

        // Debounce: wait a bit and read again
        vTaskDelay(pdMS_TO_TICKS(20));
        level = gpio_get_level(BSP_BUTTON_GPIO);
        bool pressed_debounced = (BSP_BUTTON_ACTIVE_LOW ? (level == 0) : (level == 1));

        if (pressed_debounced != last_state) {
            // State changed
            if (pressed_debounced) {
                // Button pressed
                ESP_LOGI(TAG, "Button PRESSED");
                press_start_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
                s_button_ctx.is_pressed = true;

                if (s_button_ctx.callback) {
                    s_button_ctx.callback(BSP_BUTTON_EVENT_PRESSED, s_button_ctx.user_data);
                }

                // Start long press timer
                if (s_button_ctx.long_press_timer) {
                    xTimerStart(s_button_ctx.long_press_timer, 0);
                }
            } else {
                // Button released
                uint32_t press_duration_ms = (xTaskGetTickCount() * portTICK_PERIOD_MS) - press_start_ms;
                ESP_LOGI(TAG, "Button RELEASED (held for %lu ms)", press_duration_ms);
                s_button_ctx.is_pressed = false;

                // Stop long press timer
                if (s_button_ctx.long_press_timer) {
                    xTimerStop(s_button_ctx.long_press_timer, 0);
                }

                if (s_button_ctx.callback) {
                    s_button_ctx.callback(BSP_BUTTON_EVENT_RELEASED, s_button_ctx.user_data);

                    // Short click if released before long press threshold
                    if (press_duration_ms < BSP_LONG_PRESS_MS) {
                        ESP_LOGI(TAG, "Short click detected");
                        s_button_ctx.callback(BSP_BUTTON_EVENT_SHORT_CLICK, s_button_ctx.user_data);
                    }
                }
            }

            last_state = pressed_debounced;
        }

        vTaskDelay(pdMS_TO_TICKS(50)); // Poll every 50ms
    }
}

esp_err_t bsp_button_init(bsp_button_callback_t callback, void *user_data)
{
    ESP_LOGI(TAG, "Initializing BOOT button (GPIO %d)", BSP_BUTTON_GPIO);

    // Store callback
    s_button_ctx.callback = callback;
    s_button_ctx.user_data = user_data;
    s_button_ctx.is_pressed = false;

    // Configure GPIO (polling only, no interrupts)
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BSP_BUTTON_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,  // BOOT button needs pullup
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,    // Use polling only
    };

    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure GPIO: %s", esp_err_to_name(ret));
        return ret;
    }

    // Create long press timer
    s_button_ctx.long_press_timer = xTimerCreate(
        "button_lp_timer",
        pdMS_TO_TICKS(BSP_LONG_PRESS_MS),
        pdFALSE,  // One-shot timer
        NULL,
        long_press_timer_callback
    );

    if (s_button_ctx.long_press_timer == NULL) {
        ESP_LOGE(TAG, "Failed to create long press timer");
        return ESP_ERR_NO_MEM;
    }

    // Create button task (using internal RAM for stack - more reliable during init)
    TaskHandle_t task_handle = NULL;
    BaseType_t task_ret = xTaskCreatePinnedToCore(
        button_task,
        "button_task",
        3072,  // Reduced stack size for internal RAM
        NULL,
        5,  // Priority
        &task_handle,
        1  // Core 1 (APP_CPU)
    );

    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create button task");
        if (s_button_ctx.long_press_timer) {
            xTimerDelete(s_button_ctx.long_press_timer, 0);
            s_button_ctx.long_press_timer = NULL;
        }
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "BOOT button initialized successfully");
    return ESP_OK;
}

esp_err_t bsp_button_deinit(void)
{
    if (s_button_ctx.long_press_timer) {
        xTimerDelete(s_button_ctx.long_press_timer, 0);
        s_button_ctx.long_press_timer = NULL;
    }

    // No ISR to remove (using polling only)
    gpio_reset_pin(BSP_BUTTON_GPIO);

    s_button_ctx.callback = NULL;
    s_button_ctx.user_data = NULL;

    ESP_LOGI(TAG, "Button deinitialized");
    return ESP_OK;
}

bool bsp_button_is_pressed(void)
{
    return s_button_ctx.is_pressed;
}
