#define MSG_BUFFER_SIZE (1024)

// should accommodate max size of messages coming from Pico or as MQTT payload
char payload_copy_0[16];
char payload_copy_1[16];
char received_copy_0[MSG_BUFFER_SIZE];
char received_copy_1[MSG_BUFFER_SIZE];
char sensors_datapoint_json_msg[MSG_BUFFER_SIZE];
char debugging_msg[MSG_BUFFER_SIZE];
char *scanned_substring;

// additional from HiveMQ
unsigned long lastMsg = 0;
char device_msg_to_mqtt[MSG_BUFFER_SIZE];
char received[MSG_BUFFER_SIZE];
int value = 0;

// the selected LED hardware to control
int ledPin = LED_BUILTIN;

// state variables. offline devices are -1, and offline sensors are -100.0
int connection_status = 0; // 0: no wifi, 1: wifi, 2: wifi+mqtt
float sensor_array[3] = {-100.0, -100.0, -100.0}; // humidity, temperature, light intensity
float sensor_array_old[3] = {-100.0, -100.0, -100.0};
int sensors_updated = 0;
int sensors_online = 0;
int sensors_online_qty = 2; // for now, humidity and temperature are being read
int device_array[2] = {-1, -1}; // LED and a placeholder device
int device_array_old[2] = {-1, -1};

// device topics - determined from elsewhere, forwarded to MCU. 
const char* topic_device0_status = "devices/LED_0/status";
const char* topic_device0_value = "devices/LED_0/value";

const char* mqtt_pubs_topic_status = "devices/LED_0/status";
const char* mqtt_subs_topic = "devices/LED_0/value";

// sensor topics, determined by the MCU and pushed to wifi when avbl
const char* topic_sensor0_status = "sensors/humidity/status";
const char* topic_sensor0_value = "sensors/humidity/value";
const char* topic_sensor1_status = "sensors/temperature/status";
const char* topic_sensor1_value = "sensors/temperature/value";
const char* topic_sensor2_status = "sensors/brightness/status";
const char* topic_sensor2_value = "sensors/brightness/value";
const char* topic_sensors_datapoint = "sensors/json"; // this topic should contain an entire data point with all sensor readings, ready to be picked up by app

const char* sensor_topics[3] = {topic_sensor0_value, topic_sensor1_value, topic_sensor2_value};

// general MCU and wifi module status topics
const char* topic_pico_status = "pico/status";
const char* topic_wifi_status = "wifi/status";
const char* topic_general = "general";

// JSON text
/*
const char* sensors_datapoint = {
    "DateTime":formatted_time,
    "EpochDateTime":epoch_time,
    "Temperature":{
      "Value":temperature,
      "Unit":unit,
    },
    "RelativeHumidity":humidity,
    "Brightness":brightness,
    "MobileLink":link,
    "Link":link
  }
  */

  /* json format has 8 string fields that need to be imputed */
  char* datetime = "0"; // desired format is "2022-07-09T12:00:00+02:00"
  char* epochdatetime = "0";
  char* temperatureval = "0";
  char* temperatureunit = "C";
  char* relhumidity = "0";
  char* brightness = "0";
  char* mobilelink = "placeholder";
  char* link = "placeholder";
  const char* sensors_datapoint_json_template = "{\"DateTime\":\"%s\",\"EpochDateTime\":%s,\"Temperature\":{\"Value\":%.1f,\"Unit\":\"%s\",},\"RelativeHumidity\":%.1f,\"Brightness\":%.1f,\"MobileLink\":\"%s\",\"Link\":\"%s\"}";
  

  