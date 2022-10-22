/**
 * Pico communicates with Wemos D1 Mini via UART; 
 * receives commands for device outputs and operation modes, 
 * and sends sensor readings (DHT22, LDR), over Wemos
 **/

/*
 * Set-up instructions for the Pico:
 * Ensure this project dir has the same parent as pico-sdk dir
 * Navigate to ../pico-wifi/build/
 * Compile the necessary makefile(s) via "cmake .."
 * Compile the pico script (.uf2 format) via "make"
 * Connect the Pico in boot mode via USB-to-microUSB cable
 * Copy the file, eject "scp pico_wifi.uf2 /media/an/RPI-RP2/pico_wifi.uf2"
*/

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
#include "global_params.h"
#include "string_templates.h"

// .c file; try changing CMakeLists
#include "sensors.c" 
#include "devices.c"

/* flags: used to indicate pending tasks in main loops of core0 and core1.
 * not used as mutex; Pico does not have CPU cycle management and thus 
 * no more than 1 thread per core */
uint8_t spf = 0; // signal time to publish sensor data
uint8_t srdf = 0; // sensor read DHT22 flag
uint8_t swf = 0; // write flag for sensor array
uint8_t srlf = 0; // sensor read ldr flag
uint8_t maf = 0; // modes active flag
uint8_t g_dcif = 0; // global device command implemented flag
uint8_t dcf = 0; // device command to be implemented
uint8_t dmf = 0; // device mode to be implemented
uint8_t service_denied = 0; // core1 can't take task as previous unfinished

/* parameters for devices and sensors, visible throughout program */
uint8_t g_device_being_changed = NO_DEVICE;
uint8_t g_devices[DEVICE_COUNT] = {0, 0};
uint8_t g_modes[DEVICE_COUNT] = {0, 0}; // index corresponds to device
float g_sensors[SENSOR_COUNT] = {0.0, 0.0, 0.0};
float g_ldr_anchor = 0.0; // last value for which device0 output changed
uint32_t g_wrap_point = 1000; // initial default for PWM
uint32_t irq_command = 0; // sent by core0 to core1
uint32_t device_mask = 1;
uint timer_count = 0;

uint32_t g_device_value = 0;
uint8_t g_device_index = LED_DEVICE;

operation_mode g_device_operation_modes[DEVICE_COUNT] = 
	{&ldr_led_response, &no_operation};
operation_mode g_device_shutdown_policies[DEVICE_COUNT] = 
	{&ldr_led_shutdown, &no_operation};

/* interrupt handler when core0(comm) has something for core1(devices/sensors);
 * many incoming device commands might hog core1 and stall sensor reading  
 * */
void core1_interrupt_handler() {
	uint8_t device_index = LED_DEVICE; 
	uint32_t device_value = 0;
	uint32_t command_type = DEVICE_OUTPUT_BIT;

	while (multicore_fifo_rvalid()){
		irq_command = multicore_fifo_pop_blocking();
	}
	multicore_fifo_clear_irq();

	if ((dmf | dcf) != 0) {
		/* service denied -> previous change flags were not cleared; 
		 * consider adding further info on what and why */
		service_denied = 1;
		return;
	}	

	/* extract the device index from the fifo command: upper 16 bits */
	while (( (irq_command & ~DEVICE_MODE_BIT) >> (16+device_index)) != 0) {
		device_index++;
	}

	/* extract desired device intensity: lower 16 bits, 0 - 65535 */
	device_value = irq_command % device_mask;

	/* save extracted data into global containers; not safe */
	g_device_index = device_index;
	g_device_value = device_value;

	/* check if the command is device output or mode, can be done with case */
	command_type = decode_command(irq_command);
	if (command_type == DEVICE_MODE_BIT) {
		dmf = 1;
	}

	if (command_type == DEVICE_OUTPUT_BIT) {
		dcf = 1;
	}

}


/* main program for core1 reads sensors based on timer flag, 
 * executes device commands in interrupt triggered by core0,
 * and maintains user-selected modes of device operation  
 * */
void core1_main() {

	// conversion factor for 12-bit adc reading of LDR
	const float ldr_cf = 100.0f / (1<<12); 

	// modulus mask for extracting device value
	device_mask = (1<<16);

	// configure the interrupt
	multicore_fifo_clear_irq();
	irq_set_exclusive_handler(SIO_IRQ_PROC1, core1_interrupt_handler);
	irq_set_enabled(SIO_IRQ_PROC1, true);

	while(1) {
		/*** sensor tasks ***/
		/* read DHT22 based on timer flag */
		if (srdf ) {
			dht_reading reading;
			uint8_t read_valid = 0;
			if (read_valid = read_from_dht(&reading)) {
				// if (reading.humidity < 0.001 
				// && reading.temp_celsius < 0.001) {/* don't write to array*/}
				swf = 1; // start writing
				g_sensors[HUMID_SENSOR] = reading.humidity;
				g_sensors[TEMP_SENSOR] = reading.temp_celsius;
				swf = 0; // end writing
				srdf = 0; // sensor has been read
			}
		}

		/* read ldr based on timer flag */
		if (srlf) {
			swf = 1; // start writing
			uint16_t ldr_reading = adc_read();
			g_sensors[LDR_SENSOR] = ldr_reading * ldr_cf;
			swf = 0; // end writing
			srlf = 0; // ldr has been read
		}

		/*** device tasks ***/
		/* if device output command was received, and no modes active, carry it out */
		if (dcf && g_modes[g_device_index] == 0) { 
			change_device_output(g_device_index, g_device_value);
			// smooth_change(g_device_value, g_device_index); 
			dcf = 0;
		}

		/* if device mode was received, change mode accordingly */
		if (dmf) {
			change_device_mode(g_device_index, g_device_value, &maf);
			dmf = 0;
		}

		/* if active device modes are on, execute mode for that device */
		if (maf) {
			for (int i = 0; i<DEVICE_COUNT; i++) {
				if (g_modes[i] == 1) {
					g_device_operation_modes[i]();
				}				
			}
		}
	}
}

/* timed sensor reading and publishing flags
 * */
bool repeating_timer_callback(struct repeating_timer *t) {
	timer_count++;

	// flag the DHT22 sensor for reading (core1)
	if (((timer_count % SENSOR_DHT_READ_PERIOD) == 0)) {
		srdf = 1; 
	}

	// flag the LDR for reading (core1)
	if (((timer_count % SENSOR_LDR_READ_PERIOD) == 0)) {
		srlf = 1; 
	}

	// flag the sensor array for publishing (core0) when no writing is done
	if (((timer_count % SENSOR_PUBLISH_PERIOD) == 0) && (swf == 0)) {
		spf = 1; 
	}

	return true;
}

/* main program for core0 
 * */
int main() {
    stdio_init_all(); 

    // initialize PWM (its counter goes to 65535)
    gpio_set_function(PWM_GPIO, GPIO_FUNC_PWM);
    uint slice_num = pwm_gpio_to_slice_num(PWM_GPIO);// map pin to PWM channel
    g_wrap_point = wrap_point_of_freq(PWM_OPERATING_FREQ);
    pwm_set_wrap(slice_num, g_wrap_point);
    pwm_set_chan_level(slice_num, PWM_CHAN_A, 0); // zero initial PWM to mosfet output
    pwm_set_enabled(slice_num, true); // PWM enabled on that channel

    // setting up ADC0 for the light resistor: GPIO26, no pullups
    adc_init();
    adc_gpio_init(LDR_PIN);
    adc_select_input(0);

    // for reading the DHT
    gpio_init(DHT_PIN);

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
	sleep_ms(50);
	multicore_launch_core1(core1_main);

	uart_puts(UART_ID, "C0=[Putting the first few characters to UART...];\n");
	int payload_size = 0;

	/* Pico default CPU speed at 125 MHz is faster than UART at 115200 baud rate.
	 * So since Pico's looping is faster, uart_is_readable may return false before 
	 * the entire message is received from the Wemos. Thus, message processing 
	 * must be done only after end of message is detected, \n */
    while (1) {

    	/*** UART message extraction from Rx and echoing to Tx ***/
    	if (uart_is_readable(UART_ID)) {
    		/* if uart_is_readable but no character available, uart_getc will block */
			char ch = uart_getc(UART_ID);
			if(ch!='\n' && ch!='\r') {
				msg_from_wifi[payload_size++] = ch;
			} else {
				msg_from_wifi[payload_size] = '\0';

				if (payload_size != 0) {
					/* package as comment and echo everything */
					sprintf(comment_buffer_out, comment_message_format, PICO_COMMENTS, msg_from_wifi);
					uart_puts(UART_ID, comment_buffer_out);
					uart_puts(UART_ID, "\n");	

					/** handle incoming commands **/
					/* device output command */
					if ((strstr(msg_from_wifi, device_message) != NULL)) {
					    uint32_t device_value = 0;
					    uint32_t device_index = 0;
					    sscanf(msg_from_wifi, device_message_format, &device_index, &device_value);

					    // package the device specifier into val, send data to core1 if no mode active					    
						device_value = encode_command(device_value, device_index, DEVICE_OUTPUT_BIT);
						if (g_modes[device_index] == 0 && multicore_fifo_wready()) {
							multicore_fifo_push_blocking(device_value);
						}						
					}

					/* device mode command */
					if (strstr(msg_from_wifi, mode_message) != NULL) {
					    uint32_t device_mode = 0;
					    uint32_t device_index = 0;
					    sscanf(msg_from_wifi, mode_message_format, &device_index, &device_mode);

					    // package the device specifier into val, send data to core1
					    device_mode = encode_command(device_mode, device_index, DEVICE_MODE_BIT);
						if (multicore_fifo_wready()) {
							multicore_fifo_push_blocking(device_mode);
						}
					}

				}
				payload_size = 0;
			} 
		}

		/*** sending messages over UART to the wemos ***/
		/* write stable sensor data to Tx periodically */
		if (spf && !swf) {
			for(int i = 0; i < SENSOR_COUNT; i++) {
				sprintf(sensor_buffer_out, sensor_message_format, i, g_sensors[i]);
				uart_puts(UART_ID, sensor_buffer_out);
				uart_puts(UART_ID, "\n");				
			}
			spf = 0; 
		}

		/* write the implemented device value to Tx once it is 
		 * carried out and written to D array */
		if (g_dcif && (g_device_being_changed > NO_DEVICE)) {
			sprintf(device_buffer_out, device_message_format, 
				g_device_being_changed, g_devices[g_device_being_changed]);
			uart_puts(UART_ID, device_buffer_out);
			uart_puts(UART_ID, "\n");
			g_device_being_changed = NO_DEVICE;
			g_dcif = 0;
		}

		/* inform the WiFi module if a service has been denied */
		if (service_denied) {
			sprintf(comment_buffer_out, comment_message_format, PICO_COMMENTS, service_denied_default);
			uart_puts(UART_ID, comment_buffer_out);
			uart_puts(UART_ID, "\n");					
			service_denied = 0;
		}

    }
}
