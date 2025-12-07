
# Gather common sources - exclude AEC source (requires esp-sr)
# Use explicit file list to avoid including capture_aud_aec_src.c

# Main capture sources
file(GLOB CAPTURE_MAIN_SRC "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c")

# Implementation sources (exclude AEC)
file(GLOB CAPTURE_SIMPLE_PATH_SRC "${CMAKE_CURRENT_SOURCE_DIR}/src/impl/capture_simple_path/*.c")
file(GLOB CAPTURE_FILE_SRC "${CMAKE_CURRENT_SOURCE_DIR}/src/impl/capture_file_src/*.c")
file(GLOB CAPTURE_AUDIO_ENC_SRC "${CMAKE_CURRENT_SOURCE_DIR}/src/impl/capture_audio_enc/*.c")

# Audio codec source only (not AEC source)
set(CAPTURE_AUDIO_SRC "${CMAKE_CURRENT_SOURCE_DIR}/src/impl/capture_audio_src/capture_audio_codec_src.c")

# Combine all sources
set(ALL_SRCS
    ${CAPTURE_MAIN_SRC}
    ${CAPTURE_SIMPLE_PATH_SRC}
    ${CAPTURE_FILE_SRC}
    ${CAPTURE_AUDIO_ENC_SRC}
    ${CAPTURE_AUDIO_SRC}
)

idf_component_register(
    SRCS ${ALL_SRCS}
    INCLUDE_DIRS ./include ./interface
    REQUIRES media_lib_sal esp_timer esp_audio_codec esp_codec_dev
)
