#ifndef STRING_TEMPLATES_H
#define STRING_TEMPLATES_H

#define BUFFER_SIZE 512
#define SENSOR_BUFFER 16

// text templates 
const char* device0_message = "D0=";
const char* mode0_message = "M0=";
const char* sensor_message = "S";
const char* substring_from_command;

char* device_value_from_command;
char* device_index_from_command;
char msg_copy[BUFFER_SIZE];

const char* pico_response_title = "\nPICO_ECHO: ";
char msg_from_wifi[BUFFER_SIZE];
char sensor_buffer_out[SENSOR_BUFFER];

#endif