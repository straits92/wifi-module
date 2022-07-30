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

/* take the remote command between 0 and 127 and convert to between 0 and 1000 for PWM */
long map_to_pwm(long x, long in_min, long in_max, long out_min, long out_max) {
	return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

/* gradual change from current value to desired value of device */
void smooth_change(uint8_t desired_intensity, uint8_t *device_array, uint device_index, uint32_t wrap_point) {
	uint8_t current_intensity = device_array[device_index];

	if (desired_intensity == current_intensity) {
		return;
	}

	long pwm_value_desired = map_to_pwm((long)desired_intensity, 0, 128, 0, wrap_point);
	long pwm_value_current = map_to_pwm((long)current_intensity, 0, 128, 0, wrap_point);
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

	// make array update safer
	device_array[device_index] = desired_intensity;
}
