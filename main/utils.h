#ifndef UTILS_H
#define UTILS_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "config.h"

// ============================
// Logging Macros
// ============================
#define LOG_D(tag, fmt, ...)  ESP_LOGD(tag, fmt, ##__VA_ARGS__)
#define LOG_I(tag, fmt, ...)  ESP_LOGI(tag, fmt, ##__VA_ARGS__)
#define LOG_W(tag, fmt, ...)  ESP_LOGW(tag, fmt, ##__VA_ARGS__)
#define LOG_E(tag, fmt, ...)  ESP_LOGE(tag, fmt, ##__VA_ARGS__)

// ============================
// Time Helpers
// ============================
static inline uint32_t millis(void) {
    return (uint32_t)(esp_timer_get_time() / 1000);
}

// ============================
// MAC Helpers
// ============================
void macToStr(const uint8_t* mac, char* buf);
bool macEqual(const uint8_t* a, const uint8_t* b);
void randomMac(uint8_t* mac);

// ============================
// Serial Input Helpers
// ============================
int readCharFromSerial(void);
int readIntFromSerial(void);
void serialFlush(void);

#endif // UTILS_H
