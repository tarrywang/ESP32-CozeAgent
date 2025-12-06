/**
 * @file app_events.h
 * @brief Application event system
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================
// Event System Configuration
// ============================================

#define APP_EVENT_QUEUE_SIZE    20

// ============================================
// Event Structure
// ============================================

/**
 * @brief Event data union
 */
typedef union {
    int32_t i32;
    uint32_t u32;
    float f32;
    void *ptr;
    struct {
        uint16_t x;
        uint16_t y;
    } touch;
    struct {
        uint8_t level;
        uint8_t state;
    } audio;
} app_event_data_t;

/**
 * @brief Event message structure
 */
typedef struct {
    uint32_t type;              // Event type (from app_core.h)
    uint32_t timestamp;         // Event timestamp
    app_event_data_t data;      // Event data
} app_event_msg_t;

// ============================================
// Event System Function Declarations
// ============================================

/**
 * @brief Initialize event system
 *
 * @return ESP_OK on success
 */
esp_err_t app_events_init(void);

/**
 * @brief Deinitialize event system
 *
 * @return ESP_OK on success
 */
esp_err_t app_events_deinit(void);

/**
 * @brief Post event to queue
 *
 * @param event Event message
 * @param timeout_ms Timeout in milliseconds
 * @return ESP_OK on success
 */
esp_err_t app_events_post(const app_event_msg_t *event, uint32_t timeout_ms);

/**
 * @brief Post event from ISR
 *
 * @param event Event message
 * @return ESP_OK on success
 */
esp_err_t app_events_post_from_isr(const app_event_msg_t *event);

/**
 * @brief Receive event from queue
 *
 * @param event Output event message
 * @param timeout_ms Timeout in milliseconds
 * @return ESP_OK on success, ESP_ERR_TIMEOUT on timeout
 */
esp_err_t app_events_receive(app_event_msg_t *event, uint32_t timeout_ms);

/**
 * @brief Get event queue handle
 *
 * @return Queue handle
 */
QueueHandle_t app_events_get_queue(void);

/**
 * @brief Clear all pending events
 *
 * @return ESP_OK on success
 */
esp_err_t app_events_clear(void);

#ifdef __cplusplus
}
#endif
