# ESP32-S3 Coze Voice Agent Device
# Root CMakeLists.txt

cmake_minimum_required(VERSION 3.16)

# Set local components directory
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/components")

# Optionally add ESP-ADF components if ADF_PATH is set
if(DEFINED ENV{ADF_PATH})
    list(APPEND EXTRA_COMPONENT_DIRS "$ENV{ADF_PATH}/components")
    message(STATUS "ESP-ADF components included from: $ENV{ADF_PATH}/components")
else()
    message(STATUS "ADF_PATH not set, building without ESP-ADF components")
endif()

# Include ESP-IDF project cmake
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Project name
project(coze_voice_agent)
