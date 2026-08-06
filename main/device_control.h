#ifndef DEVICE_CONTROL_H
#define DEVICE_CONTROL_H

#include <stdbool.h>
#include "config.h"

void deviceControl_begin(void);
void deviceControl_loop(void);   // call every loop for debounce

bool deviceControl_getState(DeviceId id);
void deviceControl_setState(DeviceId id, bool on, const char* source);
const char* deviceControl_getLastSource(DeviceId id);

#endif // DEVICE_CONTROL_H
