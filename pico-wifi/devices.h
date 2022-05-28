#ifndef DEVICES_H
#define DEVICES_H

#include "pico/stdlib.h"
#include "spi_params.h"

// LED intensity parameters
#define MAX_VAL 0x7F // 127; actual max 128 or 0x80
#define FLOOR_VAL 0x32 // 50; light is barely visible below this

// digipot parameters
#define REGADDR 0x00

void write_to_digipot(uint8_t intensity);
void smooth_change(uint8_t desired_intensity, uint8_t *device_array, uint device_index, int delay);

#endif