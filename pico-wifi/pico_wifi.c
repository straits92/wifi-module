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
 * LED and digipot output; send sensor data to Wemos
 **/

#include "pico/stdlib.h"
#include <stdio.h>
#include <math.h>
#include <string.h>

#include "hardware/uart.h"
#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "hardware/irq.h"
#include "pico/multicore.h"
#include "pico/binary_info.h"

#define BUFFER_SIZE 512
#define UART_ID uart0
#define PARITY UART_PARITY_NONE
#define UART_TX_PIN 0
#define UART_RX_PIN 1

#define SENSOR_COUNT 2
#define DEVICE_COUNT 1
#define SENSOR_READ_PERIOD 10000 // every 10 seconds

// flags: commands skipped if appropriate flags not switched.
// not used as mutex; Pico does not have an OS or CPU cycle management
// and thus no more than 1 thread per core
uint8_t spf = 0; // sensor publish flag
uint8_t srf = 0; // sensor read flag
uint8_t swf = 0; // sensor write flag
uint8_t sdv = 1; // sensor data validity
uint8_t def = 0; // device execution flag

uint timer_count = 0;

// for UART0
const uint LED_PIN = PICO_DEFAULT_LED_PIN;
const int baud = 115200;
const int data = 8;
const int stop = 1;

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

// for the DHT22 sensor
const uint DHT_PIN = 22;
const uint MAX_TIMINGS = 85;

// devices and sensors
uint8_t devices[DEVICE_COUNT] = {0};
float sensors[SENSOR_COUNT] = {0.0, 0.0};
uint32_t command_from_core0 = 0;

// text templates 
const char* device0_message = "D0=";
const char* sensor_message = "S";
const char* substring_from_command;
const char* pico_response_title = "\nPICO_ECHO: ";
char msg_from_wifi[BUFFER_SIZE];
char sensor_buffer_out[16];

//set humidity and temp as float
typedef struct {
    float humidity;
    float temp_celsius;
} dht_reading;

void read_from_dht(dht_reading *result);

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
void smooth_change(uint8_t desired_intensity, uint device_index, int delay) {
	uint8_t current_intensity = devices[device_index];

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
	devices[device_index] = desired_intensity;
}

// interrupt handler for whenever core0(comm) has something for core1(devices/sensors)
void core1_interrupt_handler() {
	while (multicore_fifo_rvalid()){
		command_from_core0 = multicore_fifo_pop_blocking();
	}

	// clear the interrupt. or do this only when the command is popped off?
	multicore_fifo_clear_irq();
	// flag to signify there's something to execute
	def = 1;

	// executing device commands in the interrupt has the 
	// potential of hogging core1 thread and not reading
	// the sensors, if commands are sent rapidly, 1 per sec
	smooth_change(command_from_core0, 0, 20);
}


/* main program for core1 runs an infinite loop waiting for interrupts */
void core1_entry() {

	// configure the DHT22
	gpio_init(DHT_PIN);

	// configure the interrupt
	multicore_fifo_clear_irq();
	irq_set_exclusive_handler(SIO_IRQ_PROC1, core1_interrupt_handler);
	irq_set_enabled(SIO_IRQ_PROC1, true);

	while(1) {
		tight_loop_contents();

		// read DHT22 based on timer flag
		if (srf) {

			dht_reading reading;
	        read_from_dht(&reading);

	        // only extract data if sensor reading was valid
	        if (sdv) {
		        float celsius = reading.temp_celsius;
		        float humidity = reading.humidity;

		        swf = 1; // write
		        sensors[0] = humidity;
		        sensors[1] = celsius;
		        swf = 0; // end writing
		        srf = 0; // sensor has been read
	    	}
		}

		// PROBLEM: misses some commands if sent in rapid succession
		if (def) {
				// the popping should later differentiate
				// which device the command is for; maybe
				// all single digit elements should specify that,
				// then, the first next device command comes in
				// multiplied by a factor of 10
			// executes last command taken out of fifo?
			// smooth_change(command_from_core0, 0, 20);
			def = 0;
		}
     
	}
}

// for the DHT22
//helper function to read from the DHT
void read_from_dht(dht_reading *result) {
    int data[5] = {0, 0, 0, 0, 0};
    uint last = 1;
    uint j = 0;

    gpio_set_dir(DHT_PIN, GPIO_OUT);
    gpio_put(DHT_PIN, 0);
    sleep_ms(20);
    //gpio_put(DHT_PIN, 1);
    //sleep_us(40);
    gpio_set_dir(DHT_PIN, GPIO_IN);

    gpio_put(LED_PIN, 1);
    for (uint i = 0; i < MAX_TIMINGS; i++) {
        uint count = 0;
        while (gpio_get(DHT_PIN) == last) {
            count++;
            sleep_us(1);
            if (count == 255) break;
        }
        last = gpio_get(DHT_PIN);
        if (count == 255) break;

        if ((i >= 4) && (i % 2 == 0)) {
            data[j / 8] <<= 1;
            if (count > 46) data[j / 8] |= 1;
            j++;
        }
    }
    gpio_put(LED_PIN, 0);

    if ((j >= 40) && (data[4] == ((data[0] + data[1] + data[2] + data[3]) & 0xFF))) {
        sdv = 1;        
        result->humidity = (float) ((data[0] << 8) + data[1]) / 10;
        if (result->humidity > 100) {
            result->humidity = data[0];
        }
        result->temp_celsius = (float) (((data[2] & 0x7F) << 8) + data[3]) / 10;
        if (result->temp_celsius > 125) {
            result->temp_celsius = data[2];
        }
        if (data[2] & 0x80) {
            result->temp_celsius = -result->temp_celsius;
        }

    } else {
    	sdv = 0;
        // printf("Bad data\n");
    }
}

// for the timer
bool repeating_timer_callback(struct repeating_timer *t) {
	timer_count++;
	srf = 1;

	// publishing sensor data 4 times slower than reading it
	if ((timer_count % 4) == 0) {spf = 1;}

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

	// start core1 activity with its own main function
	multicore_launch_core1(core1_entry);

	// set digipot low to begin with
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

    // set up the timer
    struct repeating_timer timer;
    add_repeating_timer_ms(SENSOR_READ_PERIOD, repeating_timer_callback, NULL, &timer);
    
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

					// echo
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

						// send command to device/sensor core1
						multicore_fifo_push_blocking(val);
					}

				}

				payload_size = 0;
			} 
		}

		// write to Tx if timer indicates it, and if sensor array is stable
		if (spf && !swf) {
			for(int i = 0; i < SENSOR_COUNT; i++) {
				// format sensor data
				sprintf(sensor_buffer_out, "%s%d=%f;", sensor_message, i, sensors[i]);

				// write sensor data
				uart_puts(UART_ID, sensor_buffer_out);
				uart_puts(UART_ID, "\n");				
			}

			// indicate completed writing by resetting flag
			spf = 0;
		}

    }
}
