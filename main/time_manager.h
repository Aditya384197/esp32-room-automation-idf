#ifndef TIME_MANAGER_H
#define TIME_MANAGER_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

void timeManager_begin(void);
void timeManager_loop(void);
bool timeManager_isSynced(void);
bool timeManager_isNtpSynced(void);
bool timeManager_getLocalTime(struct tm *outTime);
char* timeManager_getTimeString(void);
char* timeManager_getDateString(void);
int  timeManager_getDayOfYear(void);
uint32_t timeManager_getEpoch(void);

#endif // TIME_MANAGER_H
