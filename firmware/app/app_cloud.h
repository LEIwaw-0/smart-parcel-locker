#ifndef __APP_CLOUD_H
#define __APP_CLOUD_H

/******************************************************
**  app_cloud.h - TCP link to the locker server (port 8090).
**
**  Uplink  : HELLO, STATUS <occupancy mask> <door mask> <codes>,
**            DEPOSITED n, PICKED n, CHAT:<text>
**  Downlink: OPEN n / UNLOCK n, SET n <code6>, CLEAR n, AI:<text>
**
**  Cloud_Queue()     : post one uplink line to the link task (q_link);
**                      the link task sends it once the ESP8266 is idle.
**  Cloud_HandleLine(): parse one downlink line (runs in the UI task).
**  Cloud_StatusTick(): the 2s STATUS push, called from the UI task's
**                      deadline housekeeping.
******************************************************/
#include "app_config.h"

void Cloud_Queue(const char *line);
void Cloud_HandleLine(const char *line);
void Cloud_StatusTick(void);

#endif /* __APP_CLOUD_H */
