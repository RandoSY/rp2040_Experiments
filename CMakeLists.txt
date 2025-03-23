cmake_minimum_required(VERSION 3.13)
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(rp2040_freertos_ozone C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_subdirectory(freertos/FreeRTOS-Kernel)

add_executable(rp2040_freertos_ozone
    src/main.c
)

target_include_directories(rp2040_freertos_ozone PRIVATE
    include
)

target_link_libraries(rp2040_freertos_ozone
    pico_stdlib
    FreeRTOS-Kernel
    FreeRTOS-Kernel-Heap4
)

pico_add_extra_outputs(rp2040_freertos_ozone)
