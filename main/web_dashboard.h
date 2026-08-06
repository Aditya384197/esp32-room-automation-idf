#ifndef WEB_DASHBOARD_H
#define WEB_DASHBOARD_H

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

void webDashboard_begin(void);
void webDashboard_loop(void);
void webDashboard_broadcastState(void);
bool webDashboard_isApMode(void);

#ifdef __cplusplus
}
#endif

#endif // WEB_DASHBOARD_H
