#include "devices.h"

void write_to_digipot(uint8_t intensity) {
	uint8_t data[2];
	data[0] = REGADDR & 0x7F; // set msb=0 to indicate write op
	data[1] = intensity; // actual value to be written

	// do the sending
	gpio_put(CS, 0); // set chip select to "write"
	spi_write_blocking(SPI_PORT, data, 2);
	gpio_put(CS, 1);
}

// gradual change from current value to desired value
void smooth_change(uint8_t desired_intensity, uint8_t *device_array, uint device_index, int delay) {
	uint8_t current_intensity = device_array[device_index];

	if (desired_intensity == current_intensity) {
		return;
	}
	else if (desired_intensity > current_intensity) {
		for (uint8_t i = current_intensity; i < desired_intensity; i++) {
			sleep_ms(delay);
			write_to_digipot(i);		
		}
	} else {
		for (uint8_t j = current_intensity; j > desired_intensity; j--) {
			sleep_ms(delay);
			write_to_digipot(j);		
		}
	}
	write_to_digipot(desired_intensity); // avoid off by one

	// make array update safer
	device_array[device_index] = desired_intensity;
}
