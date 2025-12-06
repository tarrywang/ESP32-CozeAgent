idf_component_register(
    SRCS
        "i2s_es8311_example.c"
        "status_display.cpp"
    PRIV_REQUIRES
        esp_driver_i2s
        esp_driver_gpio
        esp_driver_i2c
        esp_driver_spi
        esp_lcd
        esp_timer
        lvgl
        SensorLib
        esp_lcd_sh8601
    EMBED_FILES "canon.pcm"
)
