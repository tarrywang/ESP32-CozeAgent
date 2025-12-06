# Azure OpenAI Realtime Component CMakeLists.txt

idf_component_register(
    SRCS
        "azure_realtime.c"
        "azure_protocol.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        esp_event
        freertos
    PRIV_REQUIRES
        cjson
        esp_websocket_client
        mbedtls
        log
        app_core
)
