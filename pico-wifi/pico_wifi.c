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

#define PRINT_TO_UART 1
#define BUFFER_SIZE 512
#define UART_ID uart0
#define PARITY    UART_PARITY_NONE
#define UART_TX_PIN 0
#define UART_RX_PIN 1

const uint LED_PIN = PICO_DEFAULT_LED_PIN;
const int baud = 115200;
const int data = 8;
const int stop = 1;
const char* device_on = "device=on";
const char* device_off = "device=off";
const char* wifi_connected = "WiFi connected";
const char* generic = "Substring received from Rx by Pico:";
char tmp_msg[BUFFER_SIZE];


int main() {
	// usb output
    stdio_init_all(); 

    // setting up UART
    uart_init(UART_ID, baud);
	uart_set_format(UART_ID, data, stop, UART_PARITY_NONE);
    gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);
    
    // the LED
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);   
    
	uart_puts(UART_ID, "PICO INIT LINE 1\n");

    while (1) {
		if (uart_is_readable(UART_ID)) {
			
			int i = 0;
			while (uart_is_readable(UART_ID)) {				
				// read char and skip newline
				char ch = uart_getc(UART_ID);
				if(ch!='\n' && ch!='\r') {
					tmp_msg[i] = ch;
					i++;
				} else {
					tmp_msg[i] = '\0'; 
					break;
				}

			}

			/* Even though 24 characters are put, 4*24 = 96 are received 
			 * by the Wemos */
			if (PRINT_TO_UART != 0) {
				uart_puts(UART_ID, "xxxxxxxxxxxxxxxxxxxxxxxx");
			}
		
			// only handle nonzero payloads
			if ( i != 0 ) {
				// check if msg fits on/off template
				if (strstr(tmp_msg, device_on) != NULL) {
					gpio_put(LED_PIN, 1);

				}
				if (strstr(tmp_msg, device_off) != NULL) {
					//device_state = 0;
					gpio_put(LED_PIN, 0);

				} 

				/* this exact message is received by the Wemos, without 
				 * duplication; but only after the  24-x string is sent
				 * 3 out of 4 times. Why? */
				uart_puts(UART_ID, "\nReceived: ");	
				uart_puts(UART_ID, tmp_msg);
				uart_putc(UART_ID, '\n');
			}
		} 
    }
}
