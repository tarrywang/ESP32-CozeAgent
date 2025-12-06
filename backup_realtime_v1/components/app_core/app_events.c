/**
 * @file app_events.c
 * @brief Application event system implementation
 */

#include "app_events.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_log.h"

static const char *TAG = "APP_EVENTS";

// ============================================
// Private Variables
// ============================================

static bool s_initialized = false;
static QueueHandle_t s_event_queue = NULL;

// ============================================
// Public Functions
// ============================================

esp_err_t app_events_init(void)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "Event system already initialized");
        return ESP_OK;
    }

    s_event_queue = xQueueCreate(APP_EVENT_QUEUE_SIZE, sizeof(app_event_msg_t));
    if (s_event_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create event queue");
        return ESP_ERR_NO_MEM;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "Event system initialized");
    return ESP_OK;
}

esp_err_t app_events_deinit(void)
{
    if (!s_initialized) {
        return ESP_OK;
    }

    if (s_event_queue) {
        vQueueDelete(s_event_queue);
        s_event_queue = NULL;
    }

    s_initialized = false;
    ESP_LOGI(TAG, "Event system deinitialized");
    return ESP_OK;
}

esp_err_t app_events_post(const app_event_msg_t *event, uint32_t timeout_ms)
{
    if (!s_initialized || event == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xQueueSend(s_event_queue, event, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        ESP_LOGW(TAG, "Event queue full, dropping event type %lu", event->type);
        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

esp_err_t app_events_post_from_isr(const app_event_msg_t *event)
{
    if (!s_initialized || event == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    if (xQueueSendFromISR(s_event_queue, event, &xHigherPriorityTaskWoken) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }

    return ESP_OK;
}

esp_err_t app_events_receive(app_event_msg_t *event, uint32_t timeout_ms)
{
    if (!s_initialized || event == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xQueueReceive(s_event_queue, event, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

QueueHandle_t app_events_get_queue(void)
{
    return s_event_queue;
}

esp_err_t app_events_clear(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    xQueueReset(s_event_queue);
    return ESP_OK;
}
