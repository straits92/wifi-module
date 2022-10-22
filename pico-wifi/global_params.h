#ifndef GLOBAL_PARAMS_H
#define GLOBAL_PARAMS_H

#include "devices.h"
#include "sensors.h"

// mark all with g_ ??
extern uint8_t g_device_being_changed;
extern uint8_t g_dcif; // flag must be global for device policies
extern uint8_t g_devices[DEVICE_COUNT];
extern uint8_t g_modes[DEVICE_COUNT];
extern float g_sensors[SENSOR_COUNT];
extern float g_ldr_anchor; // last value for which device0 output changed
extern uint32_t g_wrap_point; // for the LED PWM

extern operation_mode g_device_operation_modes[DEVICE_COUNT];
extern operation_mode g_device_shutdown_policies[DEVICE_COUNT];

#endif