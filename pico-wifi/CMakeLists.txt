cmake_minimum_required(VERSION 3.13)

# Pull in SDK (must be before project)
include(pico_sdk_import.cmake)

project(pico_wifi C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
pico_sdk_init() 

add_executable(pico_wifi pico_wifi.c)

pico_enable_stdio_usb(pico_wifi 0)

# common dependencies
# pico_multicore later 
target_link_libraries(pico_wifi pico_stdlib hardware_spi hardware_pwm hardware_adc pico_multicore) 

# compile to several formats
pico_add_extra_outputs(pico_wifi)