#define MSG_BUFFER_SIZE (500)

char payload_copy[16];
char *substring;

// strings to be checked for
const char* device_on = "device=on";
const char* device_off = "device=off";

// device topics - determined from elsewhere, forwarded to MCU
const char* topic_device0_status = "devices/LED_0/status";
const char* topic_device0_value = "devices/LED_0/value";

const char* mqtt_pubs_topic_status = "devices/LED_0/status";
const char* mqtt_subs_topic = "devices/LED_0/value";

// sensor topics, determined by the MCU and pushed to wifi when avbl
// ...

// general MCU and wifi module status topics
const char* topic_pico_status = "pico/status";
const char* topic_wifi_status = "wifi/status";
const char* topic_general = "general";

// devices
const char* device0_message = "D0=";


// JSON text
/*
const char* DHT_hourly = {
   "DateTime":formatted_time,
    "EpochDateTime":epoch_time,
    "Temperature":{
      "Value":temperature,
      "Unit":unit,
      "UnitType":17
    },
    "RelativeHumidity":humidity,
    "MobileLink":link,
    "Link":link
  }
 */
