#ifndef TIMER_MANAGER_H
#define TIMER_MANAGER_H

#include "config.h"
#include "storage_manager.h"
#include <stdbool.h>

void timerManager_begin(void);
void timerManager_loop(void);
void timerManager_start(DeviceId id, TimerKind kind, uint32_t minutes);
void timerManager_cancel(DeviceId id, TimerKind kind);
void timerManager_cancelAllForDevice(DeviceId id);
bool timerManager_isActive(DeviceId id, TimerKind kind);
uint32_t timerManager_remainingSeconds(DeviceId id, TimerKind kind);
uint32_t timerManager_totalSeconds(DeviceId id, TimerKind kind);

#endif // TIMER_MANAGER_H
