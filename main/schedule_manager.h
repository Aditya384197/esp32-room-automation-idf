#ifndef SCHEDULE_MANAGER_H
#define SCHEDULE_MANAGER_H

#include "config.h"
#include "storage_manager.h"

void scheduleManager_begin(void);
void scheduleManager_loop(void);
void scheduleManager_set(DeviceId id, bool enabled,
                          uint8_t onHour, uint8_t onMin,
                          uint8_t offHour, uint8_t offMin);
PersistedSchedule scheduleManager_get(DeviceId id);

#endif // SCHEDULE_MANAGER_H
