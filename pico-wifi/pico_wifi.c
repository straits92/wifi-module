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
 * Pico communicates with Wemos D1 Mini via UART; 
 * receives commands for device outputs, 
 * receives commands for device operation modes
 * and sends sensor readings (DHT22, LDR) to Wemos
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
#include "global_params.h"
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
uint8_t g_dcif = 0; // global device command implemented flag

/* parameters for devices and sensors, visible throughout program */
uint8_t device_being_changed = NO_DEVICE;
uint8_t devices[DEVICE_COUNT] = {0, 0};
uint8_t modes[DEVICE_COUNT] = {0, 0}; // index corresponds to device
float sensors[SENSOR_COUNT] = {0.0, 0.0, 0.0};
float ldr_anchor = 0.0; // last value for which device0 output changed
uint32_t wrap_point = 1000; // initial default for PWM
uint32_t irq_command = 0; // sent by core0 to core1
uint timer_count = 0;
operation_mode device_operation_modes[DEVICE_COUNT] = 
	{&ldr_led_response, &no_operation};
operation_mode device_shutdown_policies[DEVICE_COUNT] = 
	{&ldr_led_shutdown, &no_operation};

/* interrupt handler when core0(comm) has something for core1(devices/sensors) */
void core1_interrupt_handler() {

	uint8_t device_index = LED_DEVICE; 
	uint8_t device_value = 0;

	while (multicore_fifo_rvalid()){
		/* data in FIFO pipe may be used to differentiate which
		 * device a queued command is for. so since the queued 
		 * data is uint32_t, maybe package as follows. the first 16
		 * bits (0 to 15) are enough for a 0 to 65535 range. every higher
		 * bit should be an identifier for the device, leaving enough
		 * space for 16 devices. */
		irq_command = multicore_fifo_pop_blocking();
	}
	multicore_fifo_clear_irq();

	// extract the device info from the fifo command; 
	// can be tested by adding a second device; breadboard LED?
	while ((irq_command>>(16+device_index)) != 0) {
		device_index++;
	}

	/* many incoming device commands might hog 
	 * core1 thread and stall the sensor reading; 
	 * all devices governed by smooth PWM output for now */
	device_value = irq_command % (1<<16);
	smooth_change(irq_command, device_index);
}

/* main program for core1 reads sensors based on timer flag, 
 * executes device commands in interrupt triggered by core0,
 * and maintains user-selected modes of device operation 
 */
void core1_entry() {

	// conversion factor for 12-bit adc reading of LDR
	const float ldr_cf = 100.0f / (1<<12); 

	// configure the interrupt
	multicore_fifo_clear_irq();
	irq_set_exclusive_handler(SIO_IRQ_PROC1, core1_interrupt_handler);
	irq_set_enabled(SIO_IRQ_PROC1, true);

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

		/* if active device modes are on, execute mode for that device */
		if (maf) {
			for (int i = 0; i<DEVICE_COUNT; i++) {
				if (modes[i] == 1) {
					device_operation_modes[i]();
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
    uint slice_num = pwm_gpio_to_slice_num(PWM_GPIO);// map pin to PWM channel
    wrap_point = wrap_point_of_freq(PWM_OPERATING_FREQ);
    pwm_set_wrap(slice_num, wrap_point);
    pwm_set_chan_level(slice_num, PWM_CHAN_A, 0); // zero initial PWM to mosfet output
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

	uart_puts(UART_ID, "C0=[Putting the first few characters to UART...];\n");
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
					/* echo everything; an echoed device command is an assumption the command was done */
					sprintf(comment_buffer_out, comment_message_format, PICO_COMMENTS, msg_from_wifi);
					uart_puts(UART_ID, comment_buffer_out);
					uart_puts(UART_ID, "\n");	

					/** handle incoming commands **/
					/* device output command */
					if ((strstr(msg_from_wifi, device_message) != NULL)) {
					    int device_value = 0;
					    int device_index = 0;
					    sscanf(msg_from_wifi, device_message_format, &device_index, &device_value);

					    // package the device specifier into val
					    device_value = device_value | ((!!device_index)<<(16+device_index));

						// send device command to core1 if no mode active for it
						if (modes[device_index] == 0) {
							multicore_fifo_push_blocking(device_value);
						}						
					}

					/* device mode command */
					if (strstr(msg_from_wifi, mode_message) != NULL) {
					    int device_mode = 0;
					    int device_index = 0;
					    sscanf(msg_from_wifi, mode_message_format, &device_index, &device_mode);
					    change_device_mode(device_index, device_mode, &maf);
					}

				}
				payload_size = 0;
			} 
		}

		/* writing to Tx when sensor data safely written to array  */
		if (spf && !swf) {
			for(int i = 0; i < SENSOR_COUNT; i++) {
				sprintf(sensor_buffer_out, sensor_message_format, i, sensors[i]);
				uart_puts(UART_ID, sensor_buffer_out);
				uart_puts(UART_ID, "\n");				
			}
			spf = 0; // sensor data has been posted
		}

		/* write the implemented device value to Tx once it is 
		 * carried out and written to D array */
		if (g_dcif && (device_being_changed > NO_DEVICE)) {
			sprintf(device_buffer_out, device_message_format, 
				device_being_changed, devices[device_being_changed]);
			uart_puts(UART_ID, device_buffer_out);
			uart_puts(UART_ID, "\n");
			device_being_changed = NO_DEVICE;
			g_dcif = 0;
		}

    }
}
