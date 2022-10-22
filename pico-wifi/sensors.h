#ifndef SENSORS_H
#define SENSORS_H

#define SENSOR_COUNT 3 // temperature, humidity, brightness
#define DEVICE_COUNT 2 // LED and a placeholder
#define TIMER_PERIOD 2000 // every 2 seconds

// reading and publishing as multiples of timer period
#define SENSOR_PUBLISH_PERIOD 10 // send to WiFi module every 2*10 seconds
#define SENSOR_DHT_READ_PERIOD 3 // *2 seconds
#define SENSOR_LDR_READ_PERIOD 1 // *2 seconds

// LDR empirical constants
#define LDR_DAYLIGHT_VISIBILITY 40
#define LDR_DARK 4
#define LDR_DELTA 2 // % brightness change for which device0 should update

// sensor indices
#define HUMID_SENSOR 0
#define TEMP_SENSOR 1
#define LDR_SENSOR 2

// used to indicate when a sensor is being read
const uint LED_PIN = PICO_DEFAULT_LED_PIN;

/* the DHT22 sensor */
const uint DHT_PIN = 22;
const uint MAX_TIMINGS = 85;

/* light intensity resistor via adc */
const uint LDR_PIN = 26;

typedef struct {
    float humidity;
    float temp_celsius;
} dht_reading;

uint8_t read_from_dht(dht_reading *result);

#endif