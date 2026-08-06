/*
 * time_manager.h
 *
 * Time management module: NTP sync, fallback time from flash,
 * time zone, and helper functions for date/time strings.
 *
 * All functions are thread-safe (use static buffers for strings).
 */

#ifndef TIME_MANAGER_H
#define TIME_MANAGER_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the time manager: set timezone, load fallback,
 *        and start SNTP client.
 */
void timeManager_begin(void);

/**
 * @brief Periodically called to handle NTP re-sync and maintain time.
 */
void timeManager_loop(void);

/**
 * @brief Check if any usable time is available (NTP or fallback).
 * @return true if time is set (either NTP or fallback), false otherwise.
 */
bool timeManager_isSynced(void);

/**
 * @brief Check if NTP sync has ever succeeded this boot.
 * @return true if NTP sync was successful.
 */
bool timeManager_isNtpSynced(void);

/**
 * @brief Get the current local time as a struct tm.
 * @param outTime Pointer to struct tm to fill.
 * @return true if time is available and filled, false otherwise.
 */
bool timeManager_getLocalTime(struct tm *outTime);

/**
 * @brief Get the current time as a string "HH:MM:SS".
 * @return Pointer to a static buffer (valid until next call).
 */
char* timeManager_getTimeString(void);

/**
 * @brief Get the current date as a string "YYYY-MM-DD".
 * @return Pointer to a static buffer (valid until next call).
 */
char* timeManager_getDateString(void);

/**
 * @brief Get the day of year (0‑365).
 * @return day of year, or -1 if time not synced.
 */
int timeManager_getDayOfYear(void);

/**
 * @brief Get current epoch time (seconds since 1970-01-01).
 * @return epoch time, or 0 if time not synced.
 */
uint32_t timeManager_getEpoch(void);

#ifdef __cplusplus
}
#endif

#endif // TIME_MANAGER_H
