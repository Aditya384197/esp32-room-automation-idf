#include "device_control.h"
#include "storage_manager.h"
#include "timer_manager.h"   // forward declaration (we'll implement later)
#include "driver/gpio.h"
#include "esp_timer.h"
#include <string.h>

// Forward declare broadcast (will be implemented in web_dashboard)
void webDashboard_broadcastState(void);

typedef struct {
    uint8_t relayPin;
    uint8_t switchPin;
    bool relayState;
    char lastSource[10];
    int lastRawReading;
    int stableReading;
    uint64_t lastEdgeUs;
    bool lastSavedState;
    bool saveDirty;
    uint64_t dirtySinceUs;
} DeviceRuntime;

static DeviceRuntime dev[DEV_COUNT];

static void applyRelay(DeviceId id) {
    gpio_set_level(dev[id].relayPin, dev[id].relayState ? RELAY_ON : RELAY_OFF);
}

void deviceControl_begin(void) {
    const uint8_t relayPins[DEV_COUNT]  = { FAN_RELAY_PIN, LED_RELAY_PIN };
    const uint8_t switchPins[DEV_COUNT] = { FAN_SWITCH_PIN, LED_SWITCH_PIN };

    for (int i = 0; i < DEV_COUNT; i++) {
        dev[i].relayPin  = relayPins[i];
        dev[i].switchPin = switchPins[i];
        strncpy(dev[i].lastSource, "boot", sizeof(dev[i].lastSource)-1);
        dev[i].lastSource[sizeof(dev[i].lastSource)-1] = '\0';

        gpio_config_t cfg_relay = {
            .pin_bit_mask = (1ULL << dev[i].relayPin),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&cfg_relay);
        gpio_set_level(dev[i].relayPin, RELAY_OFF);

        gpio_config_t cfg_switch = {
            .pin_bit_mask = (1ULL << dev[i].switchPin),
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&cfg_switch);

        // Restore state from NVS
        bool savedState = storage_loadDeviceState((DeviceId)i);
        dev[i].relayState = savedState;
        dev[i].lastSavedState = savedState;
        dev[i].saveDirty = false;
        applyRelay((DeviceId)i);

        // Prime debounce
        int r = gpio_get_level(dev[i].switchPin);
        dev[i].lastRawReading = r;
        dev[i].stableReading  = r;
        dev[i].lastEdgeUs = esp_timer_get_time();
    }
}

void deviceControl_setState(DeviceId id, bool on, const char* source) {
    strncpy(dev[id].lastSource, source, sizeof(dev[id].lastSource)-1);
    dev[id].lastSource[sizeof(dev[id].lastSource)-1] = '\0';

    bool isHumanAction = (strcmp(source, "manual") == 0) || (strcmp(source, "dashboard") == 0);
    if (isHumanAction) {
        // Cancel timers (will be implemented later)
        // timerManager_cancelAllForDevice(id); // we'll add later
    }

    if (dev[id].relayState == on) return;
    dev[id].relayState = on;
    applyRelay(id);

    dev[id].saveDirty = true;
    dev[id].dirtySinceUs = esp_timer_get_time();

    webDashboard_broadcastState();
}

bool deviceControl_getState(DeviceId id) {
    return dev[id].relayState;
}

const char* deviceControl_getLastSource(DeviceId id) {
    return dev[id].lastSource;
}

static void checkSwitch(DeviceId id) {
    DeviceRuntime *d = &dev[id];
    int reading = gpio_get_level(d->switchPin);
    uint64_t now = esp_timer_get_time();

    if (reading != d->lastRawReading) {
        d->lastRawReading = reading;
        d->lastEdgeUs = now;
        return;
    }

    if ((now - d->lastEdgeUs) < (DEBOUNCE_MS * 1000ULL)) return;

    if (reading != d->stableReading) {
        d->stableReading = reading;
        deviceControl_setState(id, !d->relayState, "manual");
    }
}

static void commitPendingSave(DeviceId id) {
    DeviceRuntime *d = &dev[id];
    if (!d->saveDirty) return;
    uint64_t now = esp_timer_get_time();
    if ((now - d->dirtySinceUs) < (STATE_SAVE_DELAY_MS * 1000ULL)) return;

    if (d->relayState != d->lastSavedState) {
        storage_saveDeviceState(id, d->relayState);
        d->lastSavedState = d->relayState;
    }
    d->saveDirty = false;
}

void deviceControl_loop(void) {
    checkSwitch(DEV_FAN);
    checkSwitch(DEV_LED);
    commitPendingSave(DEV_FAN);
    commitPendingSave(DEV_LED);
}
