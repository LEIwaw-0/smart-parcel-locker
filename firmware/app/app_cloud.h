#ifndef __APP_CLOUD_H
#define __APP_CLOUD_H

/******************************************************
**  app_cloud.h - TCP link to the locker server (port 8090).
**
**  Uplink  : HELLO, STATUS <occupancy mask> <door mask> <codes>,
**            DEPOSITED n, PICKED n
**  Downlink: OPEN n / UNLOCK n, SET n <code6>, CLEAR n, AI:<text>
**
**  Cloud_Queue()    : buffer a line when the link may be busy
**                     (called from Door_Tick()).
**  Cloud_HandleLine(): parse one downlink line (called from main).
**  Cloud_Tick()     : drains the queue when idle, re-sends HELLO after
**                     a reconnect and pushes STATUS every 2s.
******************************************************/
#include "app_config.h"

void Cloud_Queue(const char *line);
void Cloud_HandleLine(const char *line);
void Cloud_Tick(void);

#endif /* __APP_CLOUD_H */
