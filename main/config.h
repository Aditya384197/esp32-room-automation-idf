#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>
#include <stdbool.h>

// ===================== GPIO Pins =====================
#define FAN_RELAY_PIN     26
#define LED_RELAY_PIN     27
#define FAN_SWITCH_PIN    32
#define LED_SWITCH_PIN    33
#define STATUS_LED_PIN    2

// ===================== Relay Active State =====================
#define RELAY_ON   LOW
#define RELAY_OFF  HIGH

// ===================== Debounce =====================
#define DEBOUNCE_MS   45UL

// ===================== NTP =====================
#define NTP_SERVER_1        "pool.ntp.org"
#define NTP_SERVER_2        "time.google.com"
#define NTP_SERVER_3        "in.pool.ntp.org"
#define GMT_OFFSET_SEC       (5 * 3600 + 30 * 60)
#define DAYLIGHT_OFFSET_SEC  0
#define NTP_SYNC_TIMEOUT_MS       1000UL
#define NTP_RETRY_INTERVAL_MS     10000UL
#define NTP_RESYNC_INTERVAL_MS    (6UL * 60UL * 60UL * 1000UL)

// ===================== WiFi =====================
#define WIFI_AP_NAME_DEFAULT       "SmartRoom-Setup"
#define WIFI_AP_PASS_DEFAULT       ""
#define WIFI_PORTAL_TIMEOUT_SEC    60
#define WIFI_CONNECT_TIMEOUT_SEC   20
#define WIFI_RECONNECT_CHECK_MS    10000UL
#define WIFI_RECONNECT_HARD_RETRY_AFTER   4
#define WIFI_CLIENT_CONNECT_TIMEOUT_MS    15000UL

// ===================== MDNS =====================
#define MDNS_HOSTNAME   "smarthome"

// ===================== HTTP =====================
#define HTTP_PORT       80
#define WS_PATH         "/ws"

// ===================== API Key =====================
#define API_SECRET_KEY   "roomauto-2026-change-me"

// ===================== OTA =====================
#define OTA_AUTH_USER        "admin"
#define OTA_AUTH_PASS        "smarthome-ota-change-me"
#define OTA_STALL_TIMEOUT_MS   20000UL
#define OTA_MAX_FAILED_AUTH    5
#define OTA_LOCKOUT_MS          60000UL

// ===================== NVS =====================
#define NVS_NAMESPACE   "roomauto"
#define STATE_SAVE_DELAY_MS   4000UL

// ===================== Device IDs =====================
typedef enum {
    DEV_FAN = 0,
    DEV_LED = 1,
    DEV_COUNT = 2
} DeviceId;

typedef enum {
    TIMER_AUTO_ON = 0,
    TIMER_AUTO_OFF = 1,
    TIMER_KIND_COUNT = 2
} TimerKind;

static inline const char* deviceName(DeviceId id) {
    return (id == DEV_FAN) ? "fan" : "led";
}

#endif // CONFIG_H
