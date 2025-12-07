
set(component_srcdirs "src")

# AppRTC and peer implementation
set(signalling_srcdirs  "impl/apprtc_signal" "impl/whip_signal")
set(signalling_incdirs  "impl/apprtc_signal" "impl/whip_signal/include" "impl/peer_default/include")

list(APPEND component_srcdirs ${signalling_srcdirs})

idf_component_register(
    SRC_DIRS ${component_srcdirs}
    INCLUDE_DIRS ./include ${signalling_incdirs}
    REQUIRES mbedtls json esp_http_client esp_websocket_client esp_netif media_lib_sal
    esp_capture webrtc_utils esp_codec_dev av_render esp_timer esp_libsrtp
)

# Link peer_default prebuilt library with its dependencies
# The libpeer_default.a requires symbols from esp_libsrtp, and they have circular dependencies.
# Use --start-group/--end-group to resolve the circular dependencies.
set(PEER_DEFAULT_LIB "${CMAKE_CURRENT_SOURCE_DIR}/impl/peer_default/libs/${IDF_TARGET}/libpeer_default.a")
if(EXISTS ${PEER_DEFAULT_LIB})
    # Locate the esp_libsrtp library built by ESP-IDF
    set(SRTP_LIB_PATH "${CMAKE_BINARY_DIR}/esp-idf/espressif__esp_libsrtp/libespressif__esp_libsrtp.a")

    # Use --start-group/--end-group to handle circular dependencies
    target_link_libraries(${COMPONENT_LIB} INTERFACE
        "-Wl,--start-group"
        "${PEER_DEFAULT_LIB}"
        "${SRTP_LIB_PATH}"
        "-Wl,--end-group"
    )
endif()
