# Main application CMakeLists.txt

idf_component_register(
    SRCS "app_main.c"
    INCLUDE_DIRS "."
    REQUIRES
        waveshare__esp32_s3_touch_amoled_1_75
        esp_codec_dev
        audio_pipeline
        coze_ws
        azure_realtime
        ui_lvgl
        app_core
        esp_wifi
        esp_event
        nvs_flash
        esp_netif
        esp_http_client
        esp_websocket_client
        simple_button
        debug_console
)
