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
 * Characters sent via UART from Wemos D1 Mini, to Pico; control onboard 
 * LED and digipot output
 **/

#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/uart.h"
#include <string.h>
#include "hardware/gpio.h"

#include "hardware/spi.h"
#include "hardware/irq.h"
#include "pico/multicore.h"

#define BUFFER_SIZE 512
#define UART_ID uart0
#define PARITY UART_PARITY_NONE
#define UART_TX_PIN 0
#define UART_RX_PIN 1

const uint LED_PIN = PICO_DEFAULT_LED_PIN;
const int baud = 115200;
const int data = 8;
const int stop = 1;
uint8_t devices[1] = {0};
const char* pico_response_title = "\nPICO_ECHO: ";
char msg_from_wifi[BUFFER_SIZE];

// templates 
const char* device0_message = "D0=";
const char* substring_from_command;

// for SPI0
#define SPI_PORT spi0
#define COMM_SPEED 500000 // 500kHz
#define SCK 2
#define SPI_TX 3 // MOSI
#define SPI_RX 4 // unused for digipot control
#define CS 5

// light intensity parameters
#define MAX_VAL 0x64 // 100; actual max 128 or 0x80
#define FLOOR_VAL 0x32 // 50; light is barely visible below this

// digipot parameters
#define REGADDR 0x00

void write_to_digipot(uint8_t intensity) {
	// prepare data structure to be sent over SPI
	uint8_t data[1];
	data[0] = REGADDR & 0x7F; // register address; msb=0 to indicate write op
	data[1] = intensity; // actual value to be written

	// do the sending
	gpio_put(CS, 0); // set chip select to "write"
	spi_write_blocking(SPI_PORT, data, 2);
	gpio_put(CS, 1);
}

// gradual change from current value to desired value
void smooth_change(uint8_t desired_intensity, int delay) {
	uint8_t current_intensity = devices[0];

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

	// block variable, do the update, unblock variable?
	devices[0] = desired_intensity;

}

// interrupt handler for whenever core0 has something for core1
void core1_interrupt_handler() {
	while (multicore_fifo_rvalid()){
		uint32_t command_from_core0 = 0;
		command_from_core0 = multicore_fifo_pop_blocking();
		smooth_change(command_from_core0, 20);
		// write_to_digipot(command_from_core0);
	}
	multicore_fifo_clear_irq();
}

/* main program for core1 runs an infinite loop waiting for interrups */
void core1_entry() {

	// configure the interrupt
	multicore_fifo_clear_irq();
	irq_set_exclusive_handler(SIO_IRQ_PROC1, core1_interrupt_handler);
	irq_set_enabled(SIO_IRQ_PROC1, true);

	while(1) {
		tight_loop_contents();
	}
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

	// pass fcnptr to second core, start it
	multicore_launch_core1(core1_entry);

	// turn the light off to begin with
	write_to_digipot(0);
	devices[0] = 0;

    // setting up UART
    uart_init(UART_ID, baud);
	uart_set_format(UART_ID, data, stop, UART_PARITY_NONE);
    gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);
    
    // setting up the LED
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
    gpio_put(LED_PIN, 0);   
    
	uart_puts(UART_ID, "Putting the first few characters to UART...\n");
	int payload_size = 0;
    while (1) {
    	/* Pico default CPU speed is 125 MHz. UART is configured as 115200 baud rate.
    	 * The looping is quicker than the UART transmission. So uart_is_readable may
    	 * return false before the entire message is received from the Wemos. Thus,
    	 * message processing must be done only after end of message is detected, \n */
    	if (uart_is_readable(UART_ID)) {
    		/* if uart_is_readable but no character available, uart_getc will block */
			char ch = uart_getc(UART_ID);
			if(ch!='\n' && ch!='\r') {
				msg_from_wifi[payload_size++] = ch;
			} else {
				msg_from_wifi[payload_size] = '\0';
				if (payload_size != 0) {
					uart_puts(UART_ID, pico_response_title);	
					uart_puts(UART_ID, msg_from_wifi);

					// check if msg fits device0
					if (strstr(msg_from_wifi, device0_message) != NULL) {
						// extract value from msg
					    substring_from_command = strtok(msg_from_wifi, "=");
					    substring_from_command = strtok(NULL, ";");
					    int val = atoi(substring_from_command);

					    // extract the device specifier from msg
					    // read between D and = ...

					    // sanity check with onboard LED
					    if (val > 0) {
					    	gpio_put(LED_PIN, 1);

					    } else {
					    	gpio_put(LED_PIN, 0);
					    }

						// send int to device/sensor core1
						multicore_fifo_push_blocking(val);
					}

				}

				payload_size = 0;
			} 
		}

    }
}
