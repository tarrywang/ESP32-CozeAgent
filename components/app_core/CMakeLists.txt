# App Core Component CMakeLists.txt

idf_component_register(
    SRCS
        "app_core.c"
        "app_wifi.c"
        "app_events.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        freertos
        esp_event
        esp_wifi
        nvs_flash
        esp_netif
        esp_timer
        audio_pipeline
        coze_ws
        azure_realtime
        ui_lvgl
)
