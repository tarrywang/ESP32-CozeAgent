/* WebRTC Azure Integration Header
 *
 * This module integrates Azure OpenAI Realtime API with WebRTC for
 * real-time voice conversations on ESP32-S3.
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"

/**
 * @brief WebRTC Azure event types
 */
typedef enum {
    WEBRTC_AZURE_EVENT_CONNECTED,        /*!< WebRTC connection established */
    WEBRTC_AZURE_EVENT_DISCONNECTED,     /*!< WebRTC connection closed */
    WEBRTC_AZURE_EVENT_DATA_CHANNEL_OPEN,/*!< Data channel connected */
    WEBRTC_AZURE_EVENT_TRANSCRIPT,       /*!< Speech transcript received */
    WEBRTC_AZURE_EVENT_FUNCTION_CALL,    /*!< Function call received */
    WEBRTC_AZURE_EVENT_ERROR,            /*!< Error occurred */
} webrtc_azure_event_type_t;

/**
 * @brief WebRTC Azure event data
 */
typedef struct {
    webrtc_azure_event_type_t type;
    union {
        struct {
            const char *text;
        } transcript;
        struct {
            const char *name;
            const char *arguments;
        } function_call;
        struct {
            int code;
            const char *message;
        } error;
    };
} webrtc_azure_event_t;

/**
 * @brief Event callback function type
 */
typedef void (*webrtc_azure_event_cb_t)(webrtc_azure_event_t *event, void *user_data);

/**
 * @brief WebRTC Azure configuration
 */
typedef struct {
    const char *wifi_ssid;               /*!< WiFi SSID */
    const char *wifi_password;           /*!< WiFi password */
    webrtc_azure_event_cb_t event_cb;    /*!< Event callback */
    void *user_data;                     /*!< User data for callback */
} webrtc_azure_config_t;

/**
 * @brief Initialize audio board with TDM mode for AEC support
 *
 * This function initializes the audio codec (ES7210/ES8311) in TDM mode,
 * which provides 4-channel I2S data required for AEC (Acoustic Echo Cancellation).
 * Must be called before webrtc_azure_init() and before BSP audio initialization.
 *
 * TDM mode channel mapping:
 * - Channel 0: MIC input (user voice)
 * - Channel 1: Reference signal (speaker output for echo cancellation)
 */
void init_audio_board(void);

/**
 * @brief Initialize WebRTC Azure module
 *
 * @param config Configuration structure
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t webrtc_azure_init(webrtc_azure_config_t *config);

/**
 * @brief Start WebRTC connection to Azure OpenAI
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t webrtc_azure_start(void);

/**
 * @brief Stop WebRTC connection
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t webrtc_azure_stop(void);

/**
 * @brief Send text message to Azure OpenAI
 *
 * @param text Text message to send
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t webrtc_azure_send_text(const char *text);

/**
 * @brief Check if WebRTC is connected
 *
 * @return true if connected, false otherwise
 */
bool webrtc_azure_is_connected(void);

/**
 * @brief Check if WebRTC is running (started but may not be fully connected)
 *
 * Use this to check if WebRTC has been started and is in the process of
 * connecting (DTLS handshake, ICE negotiation, etc.). Use is_connected()
 * to check if fully connected and ready for communication.
 *
 * @return true if WebRTC is running, false otherwise
 */
bool webrtc_azure_is_running(void);

/**
 * @brief Query WebRTC status (for periodic monitoring)
 */
void webrtc_azure_query(void);

/**
 * @brief Deinitialize WebRTC Azure module
 */
void webrtc_azure_deinit(void);

#ifdef __cplusplus
}
#endif
