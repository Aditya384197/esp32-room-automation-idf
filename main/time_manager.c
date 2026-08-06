#include "time_manager.h"
#include "storage_manager.h"
#include "utils.h"
#include "esp_netif_sntp.h"
#include <string.h>
#include <sys/time.h>

static bool ntpSynced = false;
static bool fallbackApplied = false;
static uint64_t lastSyncAttemptUs = 0;
static uint64_t lastGoodSyncUs = 0;

static void time_sync_notification_cb(struct timeval *tv) {
    ntpSynced = true;
    lastGoodSyncUs = esp_timer_get_time();
    storage_saveLastEpoch((uint32_t)time(NULL));
    LOG_I("TIME", "NTP sync successful");
}

void timeManager_begin(void) {
    // Initialize SNTP with multiple servers (array)
    const char *servers[] = { NTP_SERVER_1, NTP_SERVER_2, NTP_SERVER_3 };
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(3, servers);
    config.sync_cb = time_sync_notification_cb;
    esp_netif_sntp_init(&config);

    // Set timezone (GMT+5:30)
    setenv("TZ", "IST-5:30", 1);
    tzset();

    uint32_t savedEpoch = storage_loadLastEpoch();
    if (savedEpoch > 0) {
        struct timeval tv;
        tv.tv_sec = savedEpoch;
        tv.tv_usec = 0;
        settimeofday(&tv, NULL);
        fallbackApplied = true;
        LOG_I("TIME", "Loaded fallback time from NVS");
    }

    lastSyncAttemptUs = esp_timer_get_time();
}

void timeManager_loop(void) {
    uint64_t now = esp_timer_get_time();

    if (!ntpSynced) {
        if ((now - lastSyncAttemptUs) > (NTP_RETRY_INTERVAL_MS * 1000ULL)) {
            lastSyncAttemptUs = now;
            esp_netif_sntp_start();
        }
        return;
    }

    if ((now - lastGoodSyncUs) > (NTP_RESYNC_INTERVAL_MS * 1000ULL)) {
        esp_netif_sntp_start();
    }
}

bool timeManager_isSynced(void) {
    return ntpSynced || fallbackApplied;
}

bool timeManager_isNtpSynced(void) {
    return ntpSynced;
}

bool timeManager_getLocalTime(struct tm *outTime) {
    if (!timeManager_isSynced()) return false;
    time_t now;
    time(&now);
    localtime_r(&now, outTime);
    return true;
}

char* timeManager_getTimeString(void) {
    static char buf[9];
    struct tm t;
    if (!timeManager_getLocalTime(&t)) {
        strcpy(buf, "--:--:--");
        return buf;
    }
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", t.tm_hour, t.tm_min, t.tm_sec);
    return buf;
}

char* timeManager_getDateString(void) {
    static char buf[11];
    struct tm t;
    if (!timeManager_getLocalTime(&t)) {
        strcpy(buf, "----:--:--");
        return buf;
    }
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d", t.tm_year+1900, t.tm_mon+1, t.tm_mday);
    return buf;
}

int timeManager_getDayOfYear(void) {
    struct tm t;
    if (!timeManager_getLocalTime(&t)) return -1;
    return t.tm_yday;
}

uint32_t timeManager_getEpoch(void) {
    if (!timeManager_isSynced()) return 0;
    return (uint32_t)time(NULL);
}
