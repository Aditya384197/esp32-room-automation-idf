#include "timer_manager.h"
#include "device_control.h"
#include "time_manager.h"
#include "utils.h"

// Forward declaration for broadcast
void webDashboard_broadcastState(void);

typedef struct {
    bool active;
    bool useEpoch;
    uint32_t endEpoch;
    uint64_t endMillisFallback;
    uint32_t totalSec;
    bool restorePending;
} TimerRuntime;

static TimerRuntime tmr[DEV_COUNT][TIMER_KIND_COUNT];

static void persist(DeviceId id, TimerKind kind) {
    TimerRuntime *r = &tmr[id][kind];
    PersistedTimer p;
    p.active = r->active && r->useEpoch;
    p.useEpoch = r->useEpoch;
    p.endEpoch = r->endEpoch;
    p.totalSec = r->totalSec;
    storage_saveTimer(id, kind, &p);
}

void timerManager_begin(void) {
    for (int i = 0; i < DEV_COUNT; i++) {
        for (int k = 0; k < TIMER_KIND_COUNT; k++) {
            TimerRuntime *r = &tmr[i][k];
            r->active = false;
            r->useEpoch = true;
            r->endEpoch = 0;
            r->endMillisFallback = 0;
            r->totalSec = 0;
            r->restorePending = false;

            PersistedTimer p = storage_loadTimer((DeviceId)i, (TimerKind)k);
            if (p.active && p.useEpoch) {
                r->endEpoch = p.endEpoch;
                r->totalSec = p.totalSec;
                r->restorePending = true;
            }
        }
    }
}

void timerManager_start(DeviceId id, TimerKind kind, uint32_t minutes) {
    if (minutes == 0) minutes = 1;
    uint32_t totalSec = minutes * 60UL;

    TimerRuntime *r = &tmr[id][kind];
    r->active = true;
    r->totalSec = totalSec;
    r->restorePending = false;

    uint32_t nowEpoch = timeManager_getEpoch();
    if (nowEpoch > 0) {
        r->useEpoch = true;
        r->endEpoch = nowEpoch + totalSec;
    } else {
        r->useEpoch = false;
        r->endMillisFallback = esp_timer_get_time() + totalSec * 1000000ULL;
        r->endEpoch = 0;
    }

    persist(id, kind);
    webDashboard_broadcastState();
}

void timerManager_cancel(DeviceId id, TimerKind kind) {
    TimerRuntime *r = &tmr[id][kind];
    r->active = false;
    r->restorePending = false;
    persist(id, kind);
    webDashboard_broadcastState();
}

void timerManager_cancelAllForDevice(DeviceId id) {
    bool changed = false;
    if (tmr[id][TIMER_AUTO_ON].active) {
        tmr[id][TIMER_AUTO_ON].active = false;
        tmr[id][TIMER_AUTO_ON].restorePending = false;
        persist(id, TIMER_AUTO_ON);
        changed = true;
    }
    if (tmr[id][TIMER_AUTO_OFF].active) {
        tmr[id][TIMER_AUTO_OFF].active = false;
        tmr[id][TIMER_AUTO_OFF].restorePending = false;
        persist(id, TIMER_AUTO_OFF);
        changed = true;
    }
    if (changed) webDashboard_broadcastState();
}

bool timerManager_isActive(DeviceId id, TimerKind kind) {
    return tmr[id][kind].active;
}

uint32_t timerManager_totalSeconds(DeviceId id, TimerKind kind) {
    return tmr[id][kind].totalSec;
}

uint32_t timerManager_remainingSeconds(DeviceId id, TimerKind kind) {
    TimerRuntime *r = &tmr[id][kind];
    if (!r->active) return 0;

    if (r->useEpoch) {
        uint32_t now = timeManager_getEpoch();
        if (now == 0 || now >= r->endEpoch) return 0;
        return r->endEpoch - now;
    }
    uint64_t now = esp_timer_get_time();
    if (now >= r->endMillisFallback) return 0;
    return (r->endMillisFallback - now) / 1000000ULL;
}

static void fire(DeviceId id, TimerKind kind) {
    bool targetState = (kind == TIMER_AUTO_ON);
    deviceControl_setState(id, targetState, "timer");
}

void timerManager_loop(void) {
    bool clockReady = timeManager_isSynced();
    uint32_t nowEpoch = clockReady ? timeManager_getEpoch() : 0;
    uint64_t nowMillis = esp_timer_get_time();

    for (int i = 0; i < DEV_COUNT; i++) {
        for (int k = 0; k < TIMER_KIND_COUNT; k++) {
            DeviceId id = (DeviceId)i;
            TimerKind kind = (TimerKind)k;
            TimerRuntime *r = &tmr[i][k];

            if (r->restorePending) {
                if (!clockReady || nowEpoch == 0) continue;
                r->restorePending = false;
                if (r->endEpoch <= nowEpoch) {
                    r->active = false;
                    fire(id, kind);
                    persist(id, kind);
                } else {
                    r->active = true;
                }
                webDashboard_broadcastState();
                continue;
            }

            if (!r->active) continue;

            if (r->useEpoch) {
                if (!clockReady || nowEpoch == 0) continue;
                if (nowEpoch >= r->endEpoch) {
                    r->active = false;
                    fire(id, kind);
                    persist(id, kind);
                    webDashboard_broadcastState();
                }
            } else {
                if (nowMillis >= r->endMillisFallback) {
                    r->active = false;
                    fire(id, kind);
                    webDashboard_broadcastState();
                }
            }
        }
    }
}
