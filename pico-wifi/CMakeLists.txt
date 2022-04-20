cmake_minimum_required(VERSION 3.12)

# Pull in SDK (must be before project)
include(pico_sdk_import.cmake)

project(pico_wifi C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
pico_sdk_init()

add_executable(pico_wifi pico_wifi.c)

# common dependencies
target_link_libraries(pico_wifi pico_stdlib hardware_uart)

# compile to several formats
pico_add_extra_outputs(pico_wifi)