#ifndef DEVICES_H
#define DEVICES_H

#include "pico/stdlib.h"
#include "spi_params.h"

// PWM constants: 
// DC dimmer has a working freq of up to 20kHz
#define PWM_GPIO 20
#define PWM_SET_DELAY 4 // sleep_ms
#define PICO_CYCLE_NS 8
#define PWM_OPERATING_FREQ 20000 //20kHz

#define MIN_INCOMING_INPUT 0
#define MAX_INCOMING_INPUT 100

/* LED DC @ 28W: sensitivity as argument value to smooth_change()
 * PWM @ 20kHz; LED sensitivity up to 10/127
 * PWM @ 200kHz, LED sensitivity up to 35/127
 */


/* percentage of duty cycle (out of 100) to which the current LED 
 * device is empirically found to be sensitive 
 */
#define LED_PWM_SENSITIVITY 8

#define DEVICE_OUTPUT_BIT 0x00000000 // most significant bit is 0
#define DEVICE_MODE_BIT 0x80000000 // most significant bit is 1

// device indices 
#define NO_DEVICE (-1)
#define LED_DEVICE 0

// digipot LED intensity and addressing parameters
#define MAX_VAL 0x7F // 127; actual max 128 or 0x80
#define FLOOR_VAL 0x32 // 50; light is barely visible below this
#define REGADDR 0x00


// function declarations
typedef void (*operation_mode)(void);

void write_to_digipot(uint8_t intensity);
uint32_t wrap_point_of_freq(uint hertz);
void smooth_change(uint8_t desired_intensity, uint device_index);
long map_to_pwm(long x, long in_min, long in_max, long out_min, long out_max);

void change_device_output(uint8_t device_index, uint32_t device_value);

void change_device_mode(uint8_t device_index, uint8_t device_mode, uint8_t *modeflag);
uint8_t ldr_led_linear(float ldr_reading);
void ldr_led_response();
void no_operation();
void ldr_led_shutdown();

uint32_t encode_command(uint32_t value, uint32_t index, uint32_t msb);
uint32_t decode_command(uint32_t cmd);
uint8_t check_core1_status(uint8_t dmf, uint8_t dcf);

#endif