#ifndef DEVICES_H
#define DEVICES_H

#include "pico/stdlib.h"
#include "spi_params.h"

// PWM constants: 
// DC dimmer has a working freq of up to 20kHz; aim at 10kHz
#define PWM_GPIO 20
#define PWM_SET_DELAY 20 // sleep_ms = 20
#define PICO_CYCLE_NS 8
#define DESIRED_CYCLE_NS 8000
#define DC_DESIRED_CYCLE_NS 100000 // 10kHz
#define PWM_OPERATING_FREQ 200000 //200kHz

/* LED DC @ 28W: sensitivity as argument value to smooth_change()
 * PWM @ 20kHz; LED sensitivity up to 10
 * PWM @ 200kHz, LED sensitivity up to 30
 */
#define MAX_LED_VAL 35 
#define MIN_LED_VAL 0

// device indices 
#define LED_DEVICE 0

// digipot LED intensity parameters
#define MAX_VAL 0x7F // 127; actual max 128 or 0x80
#define FLOOR_VAL 0x32 // 50; light is barely visible below this

// digipot parameters
#define REGADDR 0x00

typedef void (*operation_mode)(void);

void write_to_digipot(uint8_t intensity);
uint32_t wrap_point_of_freq(uint hertz);
void smooth_change(uint8_t desired_intensity/*, uint8_t *device_array*/, uint device_index/*, uint32_t wrap_point*/);
long map_to_pwm(long x, long in_min, long in_max, long out_min, long out_max);

void change_device_mode(uint8_t device_index, uint8_t device_mode, uint8_t *modeflag);
uint8_t ldr_led_linear(float ldr_reading);
void ldr_led_response();
void no_operation();
void ldr_led_shutdown();

#endif