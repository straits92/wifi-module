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

#include <stdio.h>
#include <math.h>
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
uint8_t srf = 0; // sensor read flag
uint8_t swf = 0; // sensor write flag
uint8_t def = 0; // device execution flag

/* global variables for devices and sensors */
uint8_t devices[DEVICE_COUNT] = {0};
float sensors[SENSOR_COUNT] = {0.0, 0.0};
uint32_t command_from_core0 = 0;
uint timer_count = 0;

/* interrupt handler when core0(comm) has something for core1(devices/sensors) */
void core1_interrupt_handler() {
	while (multicore_fifo_rvalid()){
		/* data in FIFO pipe may be used to differentiate which
		 * device a queued command is for. */
		command_from_core0 = multicore_fifo_pop_blocking();
	}
	multicore_fifo_clear_irq();
	def = 1; // signify that a device behaviour is to be changed

	/* rapidly executing device commands in the interrupt might hog 
	 * core1 thread and stall the sensor reading */
	smooth_change(command_from_core0, devices, 0, 20);
}

/* main program for core1 reads sensors based on timer flag, 
 * and executes device commands in interrupt triggered by core0*/
void core1_entry() {
	gpio_init(DHT_PIN);

	// configure the interrupt
	multicore_fifo_clear_irq();
	irq_set_exclusive_handler(SIO_IRQ_PROC1, core1_interrupt_handler);
	irq_set_enabled(SIO_IRQ_PROC1, true);

	while(1) {
		tight_loop_contents();

		/* read DHT22 based on timer flag */
		if (srf) {
			dht_reading reading;
	        uint8_t read_valid = 0;
	        if (read_valid = read_from_dht(&reading)) {
		        swf = 1; // start writing
		        sensors[0] = reading.humidity;
		        sensors[1] = reading.temp_celsius;
		        swf = 0; // end writing
		        srf = 0; // sensor has been read
	    	}
		}

		/* Issue: the attempt to execute device change in core1 main 
		 * and not in the interrupt may result in ignoring some commands */
		if (def) {
			// smooth_change(command_from_core0, 0, 20);
			def = 0;
		}
	}
}

bool repeating_timer_callback(struct repeating_timer *t) {
	timer_count++;
	srf = 1; // trigger a reading of the sensor in core0
	if ((timer_count % SENSOR_PUBLISH_PERIOD) == 0) {
		spf = 1; // trigger a publishing of sensor data in core1
	}
	return true;
}

int main() {
    stdio_init_all(); 

    // initialize SPI 
	spi_init(SPI_PORT, COMM_SPEED);
	gpio_set_function(SCK, GPIO_FUNC_SPI);
	gpio_set_function(SPI_TX, GPIO_FUNC_SPI);

	// initialize chip select
	gpio_init(CS);
	gpio_set_dir(CS, GPIO_OUT);
	gpio_put(CS, 1); // no communication to begin with

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
    add_repeating_timer_ms(SENSOR_READ_PERIOD, repeating_timer_callback, NULL, &timer);
    
	// start core1 activity with its own main function
	multicore_launch_core1(core1_entry);

	// set digipot low to begin with
	smooth_change(0, devices, 0, 20);

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

					/* handle incoming commands */
					if (strstr(msg_from_wifi, device0_message) != NULL) {
						// extract value from msg
					    substring_from_command = strtok(msg_from_wifi, "=");
					    substring_from_command = strtok(NULL, ";");
					    int val = atoi(substring_from_command);

					    // extract the device specifier from msg
					    // read between D and = ...

						// send command to device/sensor core1
						multicore_fifo_push_blocking(val);
					}
				}
				payload_size = 0;
			} 
		}

		/* writing to Tx when sensor data available */
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
