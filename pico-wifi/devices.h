#ifndef DEVICES_H
#define DEVICES_H

#include "pico/stdlib.h"
#include "spi_params.h"

// PWM constants: 
// DC dimmer has a working freq of up to 20kHz; aim at 10kHz
#define PWM_GPIO 20
#define PICO_CYCLE_NS 8
#define DESIRED_CYCLE_NS 8000
#define DC_DESIRED_CYCLE_NS 100000 // 10kHz
#define PWM_OPERATING_FREQ 200000 //200kHz
/* LED DC load of 28W: when PWM at 20kHz has a sensitivity up to a value of 10. 
 * when 200kHz, up to 35*/

// LED intensity parameters
#define MAX_VAL 0x7F // 127; actual max 128 or 0x80
#define FLOOR_VAL 0x32 // 50; light is barely visible below this

// digipot parameters
#define REGADDR 0x00

void write_to_digipot(uint8_t intensity);
void smooth_change(uint8_t desired_intensity, uint8_t *device_array, uint device_index, int delay, uint32_t wrap_point);
long map_to_pwm(long x, long in_min, long in_max, long out_min, long out_max);

#endif