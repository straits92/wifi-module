/*
 * Set-up instructions for the Pico:
 * Ensure this project dir has the same parent as pico-sdk dir
 * Navigate to ../pico-wifi/build/
 * Compile the necessary makefile(s) via "cmake .."
 * Compile the pico script (.uf2 format) via "make"
 * Connect the Pico in boot mode via USB-to-microUSB cable
 * Copy the file, eject "scp pico_wifi.uf2 /media/an/RPI-RP2/pico_wifi.uf2"
*/

/**
 * Pico communicates with Wemos D1 Mini via UART; receives commands for
 * digipot resistance variation, and sends sensor readings (DHT22)
 **/

#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "pico/multicore.h"
#include "pico/binary_info.h"
#include "hardware/spi.h"
#include "hardware/uart.h"
#include "hardware/pwm.h"
#include "hardware/adc.h"

#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "uart_params.h"
#include "spi_params.h"
#include "string_templates.h"

// .c file; try changing CMakeLists
#include "sensors.c" 
#include "devices.c"

/* flags: used to indicate pending tasks in main loops of core0 and core1.
 * not used as mutex; Pico does not have CPU cycle management and thus 
 * no more than 1 thread per core */
uint8_t spf = 0; // sensor publish flag
uint8_t srdf = 0; // sensor read DHT22 flag
uint8_t swf = 0; // write flag for sensor array
uint8_t srlf = 0; // sensor read ldr flag
uint8_t maf = 0; // modes active flag

/* global variables for devices and sensors */
uint8_t devices[DEVICE_COUNT] = {0, 0};
uint8_t modes[DEVICE_COUNT] = {0, 0};
float sensors[SENSOR_COUNT] = {0.0, 0.0, 0.0};
uint32_t command_from_core0 = 0;
float ldr_anchor = 0.0; // last value for which device0 output changed
uint timer_count = 0;

/* for the PWM */
uint32_t wrap_point = 1000; // default

uint32_t wrap_point_of_freq(uint hertz) {
	/* get cycle length for desired operating frequency */
	uint32_t nanos = 1000000000 / hertz; 
	/* pico has a base freq of 125MHz; 125000000000Hz. 
	 *A cycle is 8ns. The wrap point is a multiple of this. */
	return nanos / PICO_CYCLE_NS; 
}

/* device mode changes */
void change_device_mode(uint8_t device_index, uint8_t device_mode){
	modes[device_index] = device_mode;
	for (int i = 0; i< DEVICE_COUNT; i++) {
		if (modes[i] > 0) {
			maf = 1; // core1 must check listeners marked in modes[]
			return;
		}

		// if any mode was switched off, kill the device output
		// ...

	}
	maf = 0;
}

/* linear response of LED to ldr sensor readings */
uint8_t ldr_led_linear(float ldr_reading) {
	return (uint8_t) ( 
			((float)LDR_DAYLIGHT_VISIBILITY - ldr_reading)
			* (((float)MAX_LED_VAL) / ((float)LDR_DAYLIGHT_VISIBILITY)) 
			);
}

/* general response of LED to ldr sensor readings */
void ldr_led_response() {
	float ldr_reading = sensors[LDR_SENSOR];
	uint8_t linear_response = 0;
	if (ldr_reading > (float)LDR_DAYLIGHT_VISIBILITY) {
		ldr_anchor = (float)LDR_DAYLIGHT_VISIBILITY;
		linear_response = MIN_LED_VAL;
	} else if (ldr_reading < (float)LDR_DARK){
		ldr_anchor = (float)LDR_DARK;
		linear_response = MAX_LED_VAL;
	} else if (abs((int)(ldr_reading - ldr_anchor)) > LDR_DELTA) {
		ldr_anchor = ldr_reading;
		linear_response = ldr_led_linear(ldr_reading);
	} else {
		return; // method returns with no activity
	} 

	smooth_change(linear_response, devices, LED_DEVICE, wrap_point);
}

/* interrupt handler when core0(comm) has something for core1(devices/sensors) */
void core1_interrupt_handler() {

	uint device_index = LED_DEVICE; 
	uint device_value = 0;

	while (multicore_fifo_rvalid()){
		/* data in FIFO pipe may be used to differentiate which
		 * device a queued command is for. so since the queued 
		 * data is uint32_t, maybe package as follows. the first 16
		 * bits (0 to 15) are enough for a 0 to 65535 range. every higher
		 * bit should be an identifier for the device, leaving enough
		 * space for 16 devices. */
		command_from_core0 = multicore_fifo_pop_blocking();
	}
	multicore_fifo_clear_irq();

	// extract the device info from the fifo command; 0 is the first light device
	// can be tested by adding a second device; another LED
	// while ((command_from_core0>>(16+device_index)) != 0) {
	// 	device_index++;
	// }

	/* many incoming device commands might hog 
	 * core1 thread and stall the sensor reading */
	// device_value = command_from_core0 % 65535;
	smooth_change(command_from_core0, devices, device_index, wrap_point);
}

/* main program for core1 reads sensors based on timer flag, 
 * and executes device commands in interrupt triggered by core0*/
void core1_entry() {

	// conversion factor for 12-bit adc reading of LDR
	const float ldr_cf = 100.0f / (1<<12); 

	// configure the interrupt
	multicore_fifo_clear_irq();
	irq_set_exclusive_handler(SIO_IRQ_PROC1, core1_interrupt_handler);
	irq_set_enabled(SIO_IRQ_PROC1, true);

	// set device0 output (PWM to mosfet) low to begin with
	// smooth_change(0, devices, 0, wrap_point);

	/* for the AC dimmer (interrupt should read zero-crossing AC at ~100Hz)*/
	/* set up a PWM with a duty cycle % */

	while(1) {
		tight_loop_contents();

		/* read DHT22 based on timer flag */
		if (srdf) {
			dht_reading reading;
	        uint8_t read_valid = 0;
	        if (read_valid = read_from_dht(&reading)) {
		        swf = 1; // start writing
		        sensors[0] = reading.humidity;
		        sensors[1] = reading.temp_celsius;
		        swf = 0; // end writing
		        srdf = 0; // sensor has been read
	    	}
		}

		/* read ldr based on timer flag */
		if (srlf) {
			swf = 1; // start writing
			uint16_t ldr_reading = adc_read();
			sensors[2] = ldr_reading * ldr_cf;
	        swf = 0; // end writing
	        srlf = 0; // ldr has been read
		}

		/* if active device modes are on, carry them out */
		if (maf) {

			// call listener for each active mode
			// the value of the modes element i, is the number associated with listener
			// if the value is 0, no listeners called
			for (int i = 0; i<DEVICE_COUNT; i++) {

				// first try calling fcns in nested ifs. then make fcn array.
				if (i == 0) {
					if (modes[i] == 1) {
						ldr_led_response(); 
					}
				}

			}


		}


	}
}

bool repeating_timer_callback(struct repeating_timer *t) {
	timer_count++;

	// flag the DHT22 sensor for reading
	if ((timer_count % SENSOR_DHT_READ_PERIOD) == 0) {
		srdf = 1; 
	}

	// flag the LDR for reading
	if ((timer_count % SENSOR_LDR_READ_PERIOD) == 0) {
		srlf = 1; 
	}

	// flag the sensor array for publishing
	if ((timer_count % SENSOR_PUBLISH_PERIOD) == 0) {
		spf = 1; 
	}


	return true;
}

int main() {
    stdio_init_all(); 

    // initialize PWM (its counter goes to 65535)
    gpio_set_function(PWM_GPIO, GPIO_FUNC_PWM);
    uint slice_num = pwm_gpio_to_slice_num(PWM_GPIO);// see which PWM channel comes from this pin
    wrap_point = wrap_point_of_freq(PWM_OPERATING_FREQ);
    pwm_set_wrap(slice_num, wrap_point);

    // set PWM channel level as duty % = 0 to begin with
    pwm_set_chan_level(slice_num, PWM_CHAN_A, 0);
    pwm_set_enabled(slice_num, true); // PWM enabled on that channel

    // setting up ADC0 for the light resistor: GPIO26, no pullups
    adc_init();
    adc_gpio_init(LDR_PIN);
    adc_select_input(0);

    // for reading the DHT
    gpio_init(DHT_PIN);

    /*
    // initialize SPI and chip select
	spi_init(SPI_PORT, COMM_SPEED);
	gpio_set_function(SCK, GPIO_FUNC_SPI);
	gpio_set_function(SPI_TX, GPIO_FUNC_SPI);
	gpio_init(CS);
	gpio_set_dir(CS, GPIO_OUT);
	gpio_put(CS, 1); // no communication to begin with
	*/

    // setting up UART
    uart_init(UART_ID, baud);
	uart_set_format(UART_ID, data, stop, UART_PARITY_NONE);
    gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);
    
    // setting up the LED
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
    gpio_put(LED_PIN, 0);   

    // set up the timer
    struct repeating_timer timer;
    add_repeating_timer_ms(TIMER_PERIOD, repeating_timer_callback, NULL, &timer);
    
	// start core1 activity with its own main function
	multicore_launch_core1(core1_entry);

	uart_puts(UART_ID, "Putting the first few characters to UART...\n");
	int payload_size = 0;

	/* Pico default CPU speed is 125 MHz. UART is configured as 115200 baud rate.
	 * The looping is quicker than the UART transmission. So uart_is_readable may
	 * return false before the entire message is received from the Wemos. Thus,
	 * message processing must be done only after end of message is detected, \n */
    while (1) {

    	/* UART message extraction from Rx and echoing to Tx */
    	if (uart_is_readable(UART_ID)) {
    		/* if uart_is_readable but no character available, uart_getc will block */
			char ch = uart_getc(UART_ID);
			if(ch!='\n' && ch!='\r') {
				msg_from_wifi[payload_size++] = ch;
			} else {
				msg_from_wifi[payload_size] = '\0';

				if (payload_size != 0) {
					// echo
					uart_puts(UART_ID, pico_response_title);	
					uart_puts(UART_ID, msg_from_wifi);

					/** handle incoming commands **/
					/* device0 output command; no commands taken when active mode for device */
					if ((strstr(msg_from_wifi, device0_message) != NULL) && (maf == 0)) {

					/*
					if (msg_from_wifi[0] == 'D') {

						// make a local copy of the message 
						strcpy(msg_copy, msg_from_wifi);

					    // extract the device specifier number from msg, between D and =
					    device_index_from_command = strtok(msg_from_wifi, "D");
					    device_index_from_command = strtok(NULL, "=");
					    int device_index = 0;
					    device_index = atoi(device_index_from_command);

						// extract value from msg between = and ;
					    device_value_from_command = strtok(msg_copy, "=");
					    device_value_from_command = strtok(NULL, ";");
					    int val = atoi(device_value_from_command);

					    // package the device specifier into val
					    val = val | ((!!device_index)<<(16+device_index));
				    */

						// extract value from msg
					    substring_from_command = strtok(msg_from_wifi, "=");
					    substring_from_command = strtok(NULL, ";");
					    int val = atoi(substring_from_command);

					    // extract the device specifier from msg
					    // read between D and = ...

						// send command to device/sensor core1
						multicore_fifo_push_blocking(val);
					}

					/* device mode command: let device0 output vary based on LDR reading */
					/*

					read the string format ... maybe M0=0 meaning mode of device 0 is routine 0
					call the routine which executes this mode 
						this routine takes the last LDR ,S2, reading, 
						scales it to D0 output, 
						writes the D0
						does this continuously as long as the mode is on M0=1;


					*/
					if (strstr(msg_from_wifi, mode0_message) != NULL) {
						uint8_t device_index = 0; // should be extracted as n from Mn=v;

					    substring_from_command = strtok(msg_from_wifi, "=");
					    substring_from_command = strtok(NULL, ";");
					    uint8_t device_mode = (uint8_t) atoi(substring_from_command);

					    change_device_mode(device_index, device_mode);
					}


				}
				payload_size = 0;
			} 
		}

		/* writing to Tx when sensor data safely written to array  */
		if (spf && !swf) {
			for(int i = 0; i < SENSOR_COUNT; i++) {
				// format sensor data
				sprintf(sensor_buffer_out, "%s%d=%f;", sensor_message, i, sensors[i]);

				// write sensor data
				uart_puts(UART_ID, sensor_buffer_out);
				uart_puts(UART_ID, "\n");				
			}
			spf = 0; // sensor data has been posted
		}
    }
}
