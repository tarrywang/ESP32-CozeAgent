# Audio Pipeline Component CMakeLists.txt

idf_component_register(
    SRCS
        "audio_pipeline.c"
        "audio_recorder.c"
        "audio_player.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        freertos
        esp_ringbuf
        esp_codec_dev
        waveshare__esp32_s3_touch_amoled_1_75
    PRIV_REQUIRES
        heap
)
