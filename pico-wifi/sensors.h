#ifndef SENSORS_H
#define SENSORS_H

#define SENSOR_COUNT 2
#define DEVICE_COUNT 2
#define SENSOR_READ_PERIOD 10000 // every 10 seconds
#define SENSOR_PUBLISH_PERIOD 4 // publishing this many times slower than reading

// used to indicate when a sensor is being read
const uint LED_PIN = PICO_DEFAULT_LED_PIN;

/* the DHT22 sensor */
const uint DHT_PIN = 22;
const uint MAX_TIMINGS = 85;

typedef struct {
    float humidity;
    float temp_celsius;
} dht_reading;

uint8_t read_from_dht(dht_reading *result);

#endif