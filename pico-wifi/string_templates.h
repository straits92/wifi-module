#ifndef STRING_TEMPLATES_H
#define STRING_TEMPLATES_H

#define BUFFER_SIZE 512
#define SENSOR_BUFFER 16

// text templates 
const char* device_message = "D";
const char* sensor_message = "S";
const char* mode_message = "M";
const char* device_message_format = "D%d=%d;";
const char* sensor_message_format = "S%d=%f;";
const char* mode_message_format = "M%d=%d;";
const char* pico_response_title = "\nPICO_ECHO: ";

// buffers 
char msg_from_wifi[BUFFER_SIZE];
char sensor_buffer_out[SENSOR_BUFFER];

#endif