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
 * Characters sent via UART from Wemos D1 Mini, to Pico; toggle onboard LED on/off
 **/

#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/uart.h"
#include <string.h>
#include "hardware/gpio.h"

#define BUFFER_SIZE 512
#define UART_ID uart0
#define PARITY UART_PARITY_NONE
#define UART_TX_PIN 0
#define UART_RX_PIN 1

const uint LED_PIN = PICO_DEFAULT_LED_PIN;
const int baud = 115200;
const int data = 8;
const int stop = 1;
const char* device_on = "device=on";
const char* device_off = "device=off";
const char* pico_response_title = "\nPICO_ECHO: ";
char msg_from_wifi[BUFFER_SIZE];

int main() {
    stdio_init_all(); 

    // setting up UART
    uart_init(UART_ID, baud);
	uart_set_format(UART_ID, data, stop, UART_PARITY_NONE);
    gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);
    
    // setting up the LED
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);   
    
	uart_puts(UART_ID, "Putting the first few characters to UART...\n");
	int payload_size = 0;
    while (1) {
    	/* Pico default CPU speed is 125 MHz. UART is configured as 115200 baud rate.
    	 * The looping is quicker than the UART transmission. So uart_is_readable may
    	 * return false before the entire message is received from the Wemos. Thus,
    	 * message processing done only after end of message is detected, \n */
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
				
					// check if msg fits on/off template
					if (strstr(msg_from_wifi, device_on) != NULL) {
						gpio_put(LED_PIN, 1);
					}
					if (strstr(msg_from_wifi, device_off) != NULL) {
						gpio_put(LED_PIN, 0);
					} 

					// check if Wemos dropped internet connection
					//... decide on tasks when no internet
				}

				payload_size = 0;
			} 
		}
    }
}
