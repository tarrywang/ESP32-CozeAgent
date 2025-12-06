/**
 * @file bsp_button.c
 * @brief Button driver implementation with debouncing and event handling
 */

#include "bsp_button.h"
#include "bsp_board.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/timers.h"
#include "esp_log.h"
#include "driver/gpio.h"

static const char *TAG = "BSP_BUTTON";

// ============================================
// Private Variables
// ============================================

static bool s_button_initialized = false;
static bsp_button_config_t s_button_config;
static bsp_button_callback_t s_button_callback = NULL;
static bsp_button_state_t s_button_states[BSP_BUTTON_MAX];

// Button GPIO mapping
static const gpio_num_t s_button_gpios[BSP_BUTTON_MAX] = {
    [BSP_BUTTON_BOOT_ID] = BSP_BUTTON_BOOT,
    [BSP_BUTTON_PWR_ID] = BSP_BUTTON_PWR,
};

// Button names
static const char *s_button_names[BSP_BUTTON_MAX] = {
    [BSP_BUTTON_BOOT_ID] = "BOOT",
    [BSP_BUTTON_PWR_ID] = "PWR",
};

// Debounce timers
static TimerHandle_t s_debounce_timers[BSP_BUTTON_MAX];
static QueueHandle_t s_button_event_queue = NULL;

// Task handle
static TaskHandle_t s_button_task_handle = NULL;

// ============================================
// Private Functions
// ============================================

/**
 * @brief Button event structure for queue
 */
typedef struct {
    bsp_button_id_t id;
    bool pressed;
    uint32_t timestamp;
} button_event_t;

/**
 * @brief GPIO ISR handler
 */
static void IRAM_ATTR gpio_isr_handler(void *arg)
{
    bsp_button_id_t button_id = (bsp_button_id_t)(intptr_t)arg;

    // Get current GPIO level
    bool pressed = (gpio_get_level(s_button_gpios[button_id]) == 0);  // Active low

    // Send event to queue
    button_event_t event = {
        .id = button_id,
        .pressed = pressed,
        .timestamp = xTaskGetTickCountFromISR(),
    };

    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xQueueSendFromISR(s_button_event_queue, &event, &xHigherPriorityTaskWoken);

    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

/**
 * @brief Button processing task
 */
static void button_task(void *pvParameters)
{
    button_event_t event;
    uint32_t last_press_time[BSP_BUTTON_MAX] = {0};
    uint32_t last_release_time[BSP_BUTTON_MAX] = {0};

    while (1) {
        if (xQueueReceive(s_button_event_queue, &event, portMAX_DELAY) == pdTRUE) {
            bsp_button_state_t *state = &s_button_states[event.id];
            uint32_t now = xTaskGetTickCount();
            uint32_t time_diff_ms = (now - (event.pressed ? last_release_time[event.id] : last_press_time[event.id])) * portTICK_PERIOD_MS;

            // Debounce check
            if (time_diff_ms < s_button_config.debounce_ms) {
                continue;
            }

            if (event.pressed && !state->is_pressed) {
                // Button pressed
                state->is_pressed = true;
                state->press_time = now;
                last_press_time[event.id] = now;

                ESP_LOGD(TAG, "Button %s pressed", s_button_names[event.id]);

                if (s_button_callback) {
                    s_button_callback(event.id, BSP_BUTTON_EVENT_PRESSED, state);
                }

            } else if (!event.pressed && state->is_pressed) {
                // Button released
                state->is_pressed = false;
                state->release_time = now;
                last_release_time[event.id] = now;

                uint32_t press_duration_ms = (state->release_time - state->press_time) * portTICK_PERIOD_MS;

                ESP_LOGD(TAG, "Button %s released (held %lu ms)",
                         s_button_names[event.id], press_duration_ms);

                if (s_button_callback) {
                    s_button_callback(event.id, BSP_BUTTON_EVENT_RELEASED, state);
                }

                // Determine event type based on duration
                bsp_button_event_t btn_event;

                if (press_duration_ms >= s_button_config.long_press_ms) {
                    btn_event = BSP_BUTTON_EVENT_LONG_PRESS;
                } else {
                    // Check for double click
                    if (state->click_count > 0 &&
                        (now - state->release_time) * portTICK_PERIOD_MS < s_button_config.double_click_ms) {
                        btn_event = BSP_BUTTON_EVENT_DOUBLE_CLICK;
                        state->click_count = 0;
                    } else {
                        btn_event = BSP_BUTTON_EVENT_CLICK;
                        state->click_count = 1;
                    }
                }

                ESP_LOGI(TAG, "Button %s: %s",
                         s_button_names[event.id], bsp_button_event_to_string(btn_event));

                if (s_button_callback) {
                    s_button_callback(event.id, btn_event, state);
                }
            }
        }
    }
}

// ============================================
// Public Functions
// ============================================

esp_err_t bsp_button_init(const bsp_button_config_t *config)
{
    if (s_button_initialized) {
        ESP_LOGW(TAG, "Buttons already initialized");
        return ESP_OK;
    }

    // Use default config if not provided
    if (config == NULL) {
        s_button_config = (bsp_button_config_t)BSP_BUTTON_DEFAULT_CONFIG();
    } else {
        s_button_config = *config;
    }

    ESP_LOGI(TAG, "Initializing buttons...");

    // Create event queue
    s_button_event_queue = xQueueCreate(10, sizeof(button_event_t));
    if (s_button_event_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create button event queue");
        return ESP_ERR_NO_MEM;
    }

    // Initialize button states
    memset(s_button_states, 0, sizeof(s_button_states));

    // Install GPIO ISR service
    esp_err_t ret = gpio_install_isr_service(ESP_INTR_FLAG_LEVEL1);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to install GPIO ISR service: %s", esp_err_to_name(ret));
        vQueueDelete(s_button_event_queue);
        return ret;
    }

    // Configure button GPIOs
    for (int i = 0; i < BSP_BUTTON_MAX; i++) {
        gpio_num_t gpio = s_button_gpios[i];

        if (gpio == GPIO_NUM_NC) {
            continue;  // Skip unconfigured buttons
        }

        gpio_config_t btn_conf = {
            .pin_bit_mask = (1ULL << gpio),
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_ANYEDGE,
        };

        ret = gpio_config(&btn_conf);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to configure button %s GPIO: %s",
                     s_button_names[i], esp_err_to_name(ret));
            continue;
        }

        // Add ISR handler
        ret = gpio_isr_handler_add(gpio, gpio_isr_handler, (void *)(intptr_t)i);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to add ISR handler for button %s: %s",
                     s_button_names[i], esp_err_to_name(ret));
        }

        ESP_LOGI(TAG, "Button %s configured on GPIO %d", s_button_names[i], gpio);
    }

    // Create button processing task
    BaseType_t xReturned = xTaskCreate(
        button_task,
        "button_task",
        2048,
        NULL,
        10,
        &s_button_task_handle
    );

    if (xReturned != pdPASS) {
        ESP_LOGE(TAG, "Failed to create button task");
        return ESP_ERR_NO_MEM;
    }

    s_button_initialized = true;
    ESP_LOGI(TAG, "Buttons initialized");

    return ESP_OK;
}

esp_err_t bsp_button_deinit(void)
{
    if (!s_button_initialized) {
        return ESP_OK;
    }

    // Delete task
    if (s_button_task_handle) {
        vTaskDelete(s_button_task_handle);
        s_button_task_handle = NULL;
    }

    // Remove ISR handlers
    for (int i = 0; i < BSP_BUTTON_MAX; i++) {
        gpio_num_t gpio = s_button_gpios[i];
        if (gpio != GPIO_NUM_NC) {
            gpio_isr_handler_remove(gpio);
        }
    }

    // Delete queue
    if (s_button_event_queue) {
        vQueueDelete(s_button_event_queue);
        s_button_event_queue = NULL;
    }

    s_button_initialized = false;
    ESP_LOGI(TAG, "Buttons deinitialized");

    return ESP_OK;
}

bool bsp_button_is_pressed(bsp_button_id_t button_id)
{
    if (button_id >= BSP_BUTTON_MAX) {
        return false;
    }

    gpio_num_t gpio = s_button_gpios[button_id];
    if (gpio == GPIO_NUM_NC) {
        return false;
    }

    return gpio_get_level(gpio) == 0;  // Active low
}

esp_err_t bsp_button_get_state(bsp_button_id_t button_id, bsp_button_state_t *state)
{
    if (button_id >= BSP_BUTTON_MAX || state == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *state = s_button_states[button_id];
    return ESP_OK;
}

esp_err_t bsp_button_register_callback(bsp_button_callback_t callback)
{
    s_button_callback = callback;
    return ESP_OK;
}

esp_err_t bsp_button_wait_press(bsp_button_id_t button_id, uint32_t timeout_ms)
{
    if (button_id >= BSP_BUTTON_MAX) {
        return ESP_ERR_INVALID_ARG;
    }

    uint32_t start = xTaskGetTickCount();
    uint32_t timeout_ticks = (timeout_ms == 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);

    while ((xTaskGetTickCount() - start) < timeout_ticks || timeout_ms == 0) {
        if (bsp_button_is_pressed(button_id)) {
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    return ESP_ERR_TIMEOUT;
}

esp_err_t bsp_button_clear_events(void)
{
    if (s_button_event_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    xQueueReset(s_button_event_queue);
    return ESP_OK;
}

const char *bsp_button_get_name(bsp_button_id_t button_id)
{
    if (button_id >= BSP_BUTTON_MAX) {
        return "UNKNOWN";
    }
    return s_button_names[button_id];
}

const char *bsp_button_event_to_string(bsp_button_event_t event)
{
    switch (event) {
        case BSP_BUTTON_EVENT_NONE:         return "NONE";
        case BSP_BUTTON_EVENT_PRESSED:      return "PRESSED";
        case BSP_BUTTON_EVENT_RELEASED:     return "RELEASED";
        case BSP_BUTTON_EVENT_CLICK:        return "CLICK";
        case BSP_BUTTON_EVENT_DOUBLE_CLICK: return "DOUBLE_CLICK";
        case BSP_BUTTON_EVENT_LONG_PRESS:   return "LONG_PRESS";
        case BSP_BUTTON_EVENT_HOLD:         return "HOLD";
        default:                            return "UNKNOWN";
    }
}
