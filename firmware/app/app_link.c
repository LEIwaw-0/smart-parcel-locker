/******************************************************
**  app_link.c - the ESP8266 link task.
**
**  Sole owner of the ESP8266 driver.  Everything else talks to it through
**  q_link (uplink lines and weather-fetch requests) and reads the
**  g_link_* snapshots.
******************************************************/
#include "app_link.h"
#include "app_rtos.h"
#include "app_clock.h"    /* Clock_WxBuf / WX_HOST / WX_PATH */
#include "esp8266.h"
#include <stdio.h>
#include <string.h>

/* one weather-fetch exchange is abandoned after this long */
#define LINK_FETCH_TIMEOUT_MS   60000u

void Link_Task(void *arg)
{
    uint8_t  was_up      = 0;
    uint8_t  fetching    = 0;
    uint32_t fetch_dl    = 0;
    char     line[200];

    (void)arg;

    for (;;)
    {
        uint8_t up;

        /* 1) the AT/TCP state machine, 5ms cadence */
        ESP8266_Tick();

        /* 2) publish the link lamps */
        up = (uint8_t)(ESP8266_IsReady() ? 1 : 0);
        g_link_status = ESP8266_GetStatus();
        g_link_fetch  = (uint8_t)(ESP8266_FetchBusy() ? 1 : 0);
        g_link_up     = up;

        /* 3) HELLO on the rising edge, ahead of anything already queued */
        if (up && !was_up)
        {
            if (fetching) fetching = 0;      /* the relay link came back */
            printf("[LINK] up\r\n");
            ESP8266_SendText("HELLO");
        }
        was_up = up;

        /* 4) downlink: hand every received line to the UI task */
        {
            uint8_t guard = 0;
            while (guard < 4 && ESP8266_GetResponse(line, (uint8_t)sizeof(line)))
            {
                app_evt_t ev;
                guard++;
                ev.kind = EV_CLOUD_LINE;
                ev.a = 0; ev.b = 0; ev.c = 0; ev.x = 0; ev.y = 0;
                strncpy(ev.text, line, APP_EV_TEXT_MAX - 1);
                ev.text[APP_EV_TEXT_MAX - 1] = 0;
                App_UiPost(&ev);
            }
        }

        /* 5) one uplink step: either the tail of a weather fetch, or the
              next queued line - never both in the same 5ms slice */
        if (fetching)
        {
            uint8_t r = ESP8266_FetchPoll(Clock_WxBuf(), Clock_WxBufSize());
            if (r != 0)
            {
                uint8_t res = (uint8_t)((r == 1) ? 1 : 2);
                xQueueSend(q_clock, &res, 0);
                fetching = 0;
                printf("[LINK] weather fetch %s\r\n", (r == 1) ? "ok" : "failed");
            }
            else if ((int32_t)(App_Ms() - fetch_dl) >= 0)
            {
                /* the driver is wedged; report failure so the clock task
                   can schedule a retry instead of waiting forever */
                uint8_t res = 2;
                xQueueSend(q_clock, &res, 0);
                fetching = 0;
                printf("[LINK] weather fetch timed out\r\n");
            }
        }
        else if (up)
        {
            app_link_t req;
            if (xQueueReceive(q_link, &req, 0) == pdTRUE)
            {
                if (req.kind == LK_FETCH)
                {
                    if (ESP8266_FetchStart(WX_HOST, WX_PATH) == 0)
                    {
                        fetching = 1;
                        fetch_dl = App_Ms() + LINK_FETCH_TIMEOUT_MS;
                        printf("[LINK] weather fetch started\r\n");
                    }
                    else
                    {
                        /* reject (link busy / bad args): tell the clock task
                           so it does not sit on a dead request */
                        uint8_t res = 2;
                        xQueueSend(q_clock, &res, 0);
                    }
                }
                else
                {
                    ESP8266_SendText(req.text);
                    printf("[LINK] up: %s\r\n", req.text);
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(5));
    }
}
