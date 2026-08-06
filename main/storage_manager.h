#ifndef STORAGE_MANAGER_H
#define STORAGE_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include "config.h"

typedef struct {
    bool    enabled;
    uint8_t onHour;
    uint8_t onMin;
    uint8_t offHour;
    uint8_t offMin;
} PersistedSchedule;

typedef struct {
    bool     active;
    bool     useEpoch;
    uint32_t endEpoch;
    uint32_t totalSec;
} PersistedTimer;

typedef struct {
    char ssid[33];
    char pass[65];
} ApConfig;

void storage_begin(void);

void storage_saveDeviceState(DeviceId id, bool state);
bool storage_loadDeviceState(DeviceId id);

void storage_saveSchedule(DeviceId id, const PersistedSchedule *s);
PersistedSchedule storage_loadSchedule(DeviceId id);

void storage_saveTimer(DeviceId id, TimerKind kind, const PersistedTimer *t);
PersistedTimer storage_loadTimer(DeviceId id, TimerKind kind);

void storage_saveLastEpoch(uint32_t epochSeconds);
uint32_t storage_loadLastEpoch(void);

void storage_saveApConfig(const ApConfig *cfg);
ApConfig storage_loadApConfig(void);

void storage_factoryReset(void);

#endif // STORAGE_MANAGER_H
