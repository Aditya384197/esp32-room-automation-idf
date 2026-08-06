#include "TimeManager.h"
#include "StorageManager.h"
#include "Config.h"
#include "esp_log.h"
#include "esp_sntp.h"          // ✅ केवल यही – lwip/apps/sntp.h से नहीं
#include "esp_timer.h"
#include <string.h>
#include <sys/time.h>

static const char *TAG = "TIME";
static bool ntpSynced = false;
static bool fallbackApplied = false;
static uint64_t lastSyncAttemptUs = 0;
static uint64_t lastGoodSyncUs = 0;

static inline uint64_t micros(void) { return esp_timer_get_time(); }
static inline uint32_t millis(void) { return micros() / 1000; }

static bool trySync(uint32_t timeoutMs) {
    // SNTP पहले से ही आरंभ है – बस यह जाँचते हैं कि क्या समय सेट हो गया है
    struct timeval tv;
    if (gettimeofday(&tv, NULL) == 0 && tv.tv_sec > 0) {
        ntpSynced = true;
        lastGoodSyncUs = micros();
        storage_saveLastEpoch((uint32_t)tv.tv_sec);
        ESP_LOGI(TAG, "SNTP sync successful, epoch = %lu", (uint32_t)tv.tv_sec);
        return true;
    }
    return false;
}

void timeManager_begin() {
    // समय क्षेत्र सेट करें
    char tz[32];
    snprintf(tz, sizeof(tz), "UTC%+d", GMT_OFFSET_SEC / 3600);
    setenv("TZ", tz, 1);
    tzset();

    // फ़ॉलबैक (अंतिम ज्ञात ईपॉक) लोड करें
    uint32_t savedEpoch = storage_loadLastEpoch();
    if (savedEpoch > 0) {
        struct timeval tv;
        tv.tv_sec = (time_t)savedEpoch;
        tv.tv_usec = 0;
        settimeofday(&tv, NULL);
        fallbackApplied = true;
        ESP_LOGI(TAG, "Fallback time set to %lu", savedEpoch);
    }

    // SNTP कॉन्फ़िगर करें – सरल API (Arduino configTime के समान)
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, NTP_SERVER_1);
    esp_sntp_setservername(1, NTP_SERVER_2);
    esp_sntp_setservername(2, NTP_SERVER_3);
    esp_sntp_init();

    lastSyncAttemptUs = micros();
    ESP_LOGI(TAG, "SNTP initialized with servers: %s, %s, %s", NTP_SERVER_1, NTP_SERVER_2, NTP_SERVER_3);
}

void timeManager_loop() {
    uint64_t now = micros();

    if (!ntpSynced) {
        // पहला सिंक प्रयास – हर NTP_RETRY_INTERVAL_MS पर
        if (now - lastSyncAttemptUs > (NTP_RETRY_INTERVAL_MS * 1000ULL)) {
            lastSyncAttemptUs = now;
            trySync(NTP_SYNC_TIMEOUT_MS);
        }
        return;
    }

    // NTP पुनः सिंक – हर NTP_RESYNC_INTERVAL_MS पर
    if (now - lastGoodSyncUs > (NTP_RESYNC_INTERVAL_MS * 1000ULL)) {
        trySync(NTP_SYNC_TIMEOUT_MS);
    }
}

bool timeManager_isSynced() {
    return ntpSynced || fallbackApplied;
}

bool timeManager_isNtpSynced() {
    return ntpSynced;
}

bool timeManager_getLocalTime(struct tm *outTime) {
    if (!timeManager_isSynced()) return false;
    time_t now;
    time(&now);
    if (localtime_r(&now, outTime) == NULL) return false;
    return true;
}

char* timeManager_getTimeString(char *buf, size_t len) {
    struct tm t;
    if (!timeManager_getLocalTime(&t)) {
        snprintf(buf, len, "--:--:--");
    } else {
        snprintf(buf, len, "%02d:%02d:%02d", t.tm_hour, t.tm_min, t.tm_sec);
    }
    return buf;
}

char* timeManager_getDateString(char *buf, size_t len) {
    struct tm t;
    if (!timeManager_getLocalTime(&t)) {
        snprintf(buf, len, "----:--:--");
    } else {
        snprintf(buf, len, "%04d-%02d-%02d", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
    }
    return buf;
}

int timeManager_getDayOfYear() {
    struct tm t;
    if (!timeManager_getLocalTime(&t)) return -1;
    return t.tm_yday;
}

uint32_t timeManager_getEpoch() {
    if (!timeManager_isSynced()) return 0;
    return (uint32_t)time(NULL);
}
