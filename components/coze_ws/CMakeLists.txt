# Coze WebSocket Component CMakeLists.txt

idf_component_register(
    SRCS
        "coze_ws.c"
        "coze_protocol.c"
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
