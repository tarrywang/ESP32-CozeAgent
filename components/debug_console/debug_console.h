/**
 * @file debug_console.h
 * @brief Debug Console for Serial Commands
 */

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize debug console with serial commands
 *
 * @return esp_err_t ESP_OK on success
 */
esp_err_t debug_console_init(void);

/**
 * @brief Send a test message to Coze bot
 *
 * @param message Text message to send
 * @return esp_err_t ESP_OK on success
 */
esp_err_t debug_console_send_message(const char *message);

/**
 * @brief Trigger conversation start (simulate button press)
 *
 * @return esp_err_t ESP_OK on success
 */
esp_err_t debug_console_trigger_conversation(void);

#ifdef __cplusplus
}
#endif
