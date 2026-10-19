#ifndef __APP_LINK_H
#define __APP_LINK_H

/******************************************************
**  app_link.h - the ESP8266 link task.
**
**  Publishes the uplink/downlink of the relay TCP connection:
**
**    - keeps ESP8266_Tick() (the AT/CIPSEND state machine) running at 5ms
**    - publishes the read-only link snapshots (g_link_status / up / fetch)
**    - sends HELLO itself on the link-up edge, before queued traffic
**    - posts every downlink line as EV_CLOUD_LINE to the UI task
**    - drains q_link: one uplink line, or one weather-fetch request
**
**  When a weather fetch owns the link (ESP8266_FetchBusy()), the uplink
**  queue is deliberately left alone - the relay socket is closed for the
**  duration and any send would corrupt the HTTP exchange.
******************************************************/
#include "app_config.h"

void Link_Task(void *arg);

#endif /* __APP_LINK_H */
