#include "devices.h"

/* writes to digipot via SPI */
void write_to_digipot(uint8_t intensity) {
	uint8_t data[2];
	data[0] = REGADDR & 0x7F; // set msb=0 to indicate write op
	data[1] = intensity; // actual value to be written

	// do the sending
	gpio_put(CS, 0); // set chip select to "write"
	spi_write_blocking(SPI_PORT, data, 2);
	gpio_put(CS, 1);
}

/* calculate wrap point for the PWM. The wrap point determines
 * the multiple of Pico cycles needed to make up one full cycle
 * for a desired PWM output frequency. Hence it is a "wrap point"
 * as after a certain number of Pico cycles, the new cycle of
 * the desired frequency should begin. A smaller wrap point
 * means faster PWM variation for the higher frequency. This
 * might mean higher switching losses for the module receiving
 * the PWM signal. */
uint32_t wrap_point_of_freq(uint hertz) {
	/* get cycle length for desired operating frequency */
	uint32_t nanos = 1000000000 / hertz; 
	/* pico has a base freq of 125MHz; 125000000000Hz. 
	 *A cycle is 8ns. The wrap point is a multiple of this. */
	return nanos / PICO_CYCLE_NS; 
}

/* scale the remote command to between 0 and the g_wrap_point for PWM */
long map_to_pwm(long x, long in_min, long in_max, long out_min, long out_max) {
	return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

/* gradual change from current value to desired value of device */
/* input 0 to 100 mapped to a value between 0 and g_wrap_point.
 * the LED device is sensitive to only 10% of the duty cycle 
 * given to MOSFET 20kHz; command mapped to that 10%.
 * */
void smooth_change(uint8_t desired_intensity, uint device_index) {
	uint8_t current_intensity = g_devices[device_index];

	if (desired_intensity == current_intensity) {
		return;
	}

	/* set the scaling factor for the target device */
	uint32_t scaling_factor = LED_PWM_SENSITIVITY;
	uint32_t max_pwm_out = (g_wrap_point * scaling_factor) / 100;

	long pwm_value_desired = map_to_pwm((long)desired_intensity, 
		MIN_INCOMING_INPUT, MAX_INCOMING_INPUT, 0, max_pwm_out);
	long pwm_value_current = map_to_pwm((long)current_intensity,   
		MIN_INCOMING_INPUT, MAX_INCOMING_INPUT, 0, max_pwm_out);
	uint slice_num = pwm_gpio_to_slice_num(PWM_GPIO);

	if (pwm_value_desired > pwm_value_current) {
		for (long i = pwm_value_current; i < pwm_value_desired; i++) {
			sleep_ms(PWM_SET_DELAY);
			pwm_set_chan_level(slice_num, PWM_CHAN_A, i);
		}
	} else {
		for (long j = pwm_value_current; (j > pwm_value_desired); j--) {
			sleep_ms(PWM_SET_DELAY);
			pwm_set_chan_level(slice_num, PWM_CHAN_A, j);
		}
	}
	pwm_set_chan_level(slice_num, PWM_CHAN_A, pwm_value_desired); // avoid off by one

	// device updates only available for posting after writing done
	g_devices[device_index] = desired_intensity;
	g_device_being_changed = device_index;
	g_dcif = 1;	
}

/* device output changes; only LED device for now;
 * this can also be modelled as an array of functions 
 * */
void change_device_output(uint8_t device_index, uint32_t device_value) {
	if (device_index == LED_DEVICE) {
		smooth_change(device_value, device_index);
	}
}

/*** device mode changes ***/

/* trigger a mode change in a device; called from core0 */
void change_device_mode(uint8_t device_index, uint8_t device_mode, uint8_t *modeflag){
	g_modes[device_index] = device_mode;

	// device shutdown if a mode is turned off, or when there's a mode change
	// if (device_mode == 0) {
		g_device_shutdown_policies[device_index]();
	// }

	// check for active modes 
	for (int i = 0; i< DEVICE_COUNT; i++) {
		if (g_modes[i] > 0) {
			*modeflag = 1; // core1 must check listeners marked in g_modes[]
			return;
		}
	}
	*modeflag = 0;
}

/* linear magnitude of LED output wrt ldr sensor readings */
uint8_t ldr_led_linear(float ldr_reading) {
	return (uint8_t)( 
			((float)LDR_DAYLIGHT_VISIBILITY - ldr_reading)
			* (((float)MAX_INCOMING_INPUT) / ((float)LDR_DAYLIGHT_VISIBILITY)) 
					);
}

/* general response of LED to ldr sensor readings */
void ldr_led_response() {
	float ldr_reading = g_sensors[LDR_SENSOR];
	uint8_t linear_response = 0;
	if (ldr_reading > (float)LDR_DAYLIGHT_VISIBILITY) {
		g_ldr_anchor = (float)LDR_DAYLIGHT_VISIBILITY;
		linear_response = MIN_INCOMING_INPUT;
	} else if (ldr_reading < (float)LDR_DARK){
		g_ldr_anchor = (float)LDR_DARK;
		linear_response = MAX_INCOMING_INPUT;
	} else if (abs((int)(ldr_reading - g_ldr_anchor)) > LDR_DELTA) {
		g_ldr_anchor = ldr_reading;
		linear_response = ldr_led_linear(ldr_reading);
	} else {
		return; // method returns with no activity
	} 

	smooth_change(linear_response, LED_DEVICE);
}

/* empty mode response */
void no_operation() {
	return;
}

/* shutdown LDR-dependent LED response*/
void ldr_led_shutdown() {
	smooth_change(MIN_INCOMING_INPUT, LED_DEVICE);
}

/* device command protocol implemented here */
uint32_t encode_command(uint32_t value, uint32_t index, uint32_t msb) {
	return value | ((!!index)<<(16+index)) | msb;
}

uint32_t decode_command(uint32_t cmd) {
	return cmd & DEVICE_MODE_BIT;
}