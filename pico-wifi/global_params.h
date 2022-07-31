#ifndef GLOBAL_PARAMS_H
#define GLOBAL_PARAMS_H

#include "devices.h"
#include "sensors.h"

extern uint8_t devices[DEVICE_COUNT];
extern uint8_t modes[DEVICE_COUNT];
extern float sensors[SENSOR_COUNT];
extern float ldr_anchor; // last value for which device0 output changed
extern uint32_t wrap_point; // default for PWM

extern operation_mode device_operation_modes[DEVICE_COUNT];
extern operation_mode device_shutdown_policies[DEVICE_COUNT];

#endif