#ifndef STRING_TEMPLATES_H
#define STRING_TEMPLATES_H

#define BUFFER_SIZE 512
#define SENSOR_BUFFER 16
#define DEVICE_BUFFER 16

// enumerated comments
#define PICO_COMMENTS 0
#define WIFI_COMMENTS 1

// text templates 
const char* comment_message = "C"; // for misc. messages
const char* device_message = "D";
const char* sensor_message = "S";
const char* mode_message = "M";
const char* comment_message_format = "C%d=[%s];";
const char* device_message_format = "D%d=%d;";
const char* sensor_message_format = "S%d=%f;";
const char* mode_message_format = "M%d=%d;";
const char* pico_response_title = "PICO_ECHO";

// buffers 
char msg_from_wifi[BUFFER_SIZE];
char sensor_buffer_out[SENSOR_BUFFER];
char device_buffer_out[DEVICE_BUFFER];
char comment_buffer_out[BUFFER_SIZE];

#endif