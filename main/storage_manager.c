#include "storage_manager.h"
#include "utils.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>

static nvs_handle_t _nvs_handle = 0;
static bool _initialized = false;

static const char* stateKey(DeviceId id) {
    return (id == DEV_FAN) ? "st_fan" : "st_led";
}
static const char* schedKey(DeviceId id) {
    return (id == DEV_FAN) ? "sc_fan" : "sc_led";
}
static const char* timerKey(DeviceId id, TimerKind kind) {
    if (id == DEV_FAN) {
        return (kind == TIMER_AUTO_ON) ? "tm_fan_on" : "tm_fan_of";
    }
    return (kind == TIMER_AUTO_ON) ? "tm_led_on" : "tm_led_of";
}
#define EPOCH_KEY   "last_epoch"
#define AP_CONFIG_KEY "ap_cfg"

void storage_begin(void) {
    if (_initialized) return;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &_nvs_handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        // Erase and reinit if corrupt or new
        nvs_flash_erase();
        nvs_flash_init();
        err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &_nvs_handle);
    }
    if (err != ESP_OK) {
        LOG_E("STORAGE", "NVS open failed");
        _nvs_handle = 0;
        return;
    }
    _initialized = true;
}

void storage_saveDeviceState(DeviceId id, bool state) {
    if (!_initialized || _nvs_handle == 0) return;
    nvs_set_u8(_nvs_handle, stateKey(id), state ? 1 : 0);
    nvs_commit(_nvs_handle);
}

bool storage_loadDeviceState(DeviceId id) {
    if (!_initialized || _nvs_handle == 0) return false;
    uint8_t val = 0;
    nvs_get_u8(_nvs_handle, stateKey(id), &val);
    return (val != 0);
}

void storage_saveSchedule(DeviceId id, const PersistedSchedule *s) {
    if (!_initialized || _nvs_handle == 0) return;
    nvs_set_blob(_nvs_handle, schedKey(id), s, sizeof(PersistedSchedule));
    nvs_commit(_nvs_handle);
}

PersistedSchedule storage_loadSchedule(DeviceId id) {
    PersistedSchedule s = {0};
    if (!_initialized || _nvs_handle == 0) return s;
    size_t len = sizeof(PersistedSchedule);
    nvs_get_blob(_nvs_handle, schedKey(id), &s, &len);
    return s;
}

void storage_saveTimer(DeviceId id, TimerKind kind, const PersistedTimer *t) {
    if (!_initialized || _nvs_handle == 0) return;
    nvs_set_blob(_nvs_handle, timerKey(id, kind), t, sizeof(PersistedTimer));
    nvs_commit(_nvs_handle);
}

PersistedTimer storage_loadTimer(DeviceId id, TimerKind kind) {
    PersistedTimer t = {0};
    if (!_initialized || _nvs_handle == 0) return t;
    size_t len = sizeof(PersistedTimer);
    nvs_get_blob(_nvs_handle, timerKey(id, kind), &t, &len);
    return t;
}

void storage_saveLastEpoch(uint32_t epochSeconds) {
    if (!_initialized || _nvs_handle == 0) return;
    nvs_set_u32(_nvs_handle, EPOCH_KEY, epochSeconds);
    nvs_commit(_nvs_handle);
}

uint32_t storage_loadLastEpoch(void) {
    if (!_initialized || _nvs_handle == 0) return 0;
    uint32_t val = 0;
    nvs_get_u32(_nvs_handle, EPOCH_KEY, &val);
    return val;
}

void storage_saveApConfig(const ApConfig *cfg) {
    if (!_initialized || _nvs_handle == 0) return;
    nvs_set_blob(_nvs_handle, AP_CONFIG_KEY, cfg, sizeof(ApConfig));
    nvs_commit(_nvs_handle);
}

ApConfig storage_loadApConfig(void) {
    ApConfig cfg = {0};
    if (!_initialized || _nvs_handle == 0) {
        strcpy(cfg.ssid, WIFI_AP_NAME_DEFAULT);
        strcpy(cfg.pass, WIFI_AP_PASS_DEFAULT);
        return cfg;
    }
    size_t len = sizeof(ApConfig);
    if (nvs_get_blob(_nvs_handle, AP_CONFIG_KEY, &cfg, &len) == ESP_OK) {
        cfg.ssid[sizeof(cfg.ssid)-1] = '\0';
        cfg.pass[sizeof(cfg.pass)-1] = '\0';
    } else {
        strcpy(cfg.ssid, WIFI_AP_NAME_DEFAULT);
        strcpy(cfg.pass, WIFI_AP_PASS_DEFAULT);
    }
    return cfg;
}

void storage_factoryReset(void) {
    if (!_initialized || _nvs_handle == 0) return;
    nvs_erase_all(_nvs_handle);
    nvs_commit(_nvs_handle);
}
