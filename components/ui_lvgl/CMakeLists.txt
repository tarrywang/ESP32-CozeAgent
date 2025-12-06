# UI LVGL Component CMakeLists.txt

idf_component_register(
    SRCS
        "ui_manager.c"
        "ui_idle.c"
        "ui_listening.c"
        "ui_thinking.c"
        "ui_speaking.c"
        "ui_info_carousel.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        lvgl
        freertos
        esp_timer
        esp_lvgl_port
        waveshare__esp32_s3_touch_amoled_1_75
        display
    PRIV_REQUIRES
        system_info
)
