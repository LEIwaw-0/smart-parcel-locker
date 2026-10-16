/******************************************************
**  app_cloud.c - TCP link to the locker server (port 8090).
**
**  Split out of the monolithic main.c; bodies are unchanged.
******************************************************/
#include "app_cloud.h"
#include "app_ctx.h"
#include "app_door.h"     /* DoOpenBox */
#include "app_ui.h"       /* StatusBar_Set, PickupDone_Draw */
#include "app_voice.h"    /* Chat_HandleResponse */
#include "codebox.h"
#include "esp8266.h"
#include "lock.h"
#include <stdio.h>
#include <string.h>

/* ---- private state: pending-line queue + link bookkeeping ---- */
static char     s_cloud_pend[CLOUD_PEND_MAX][CLOUD_PEND_LEN];
static uint8_t  s_cloud_pend_n = 0;
static uint8_t  s_cloud_was_up = 0;
static uint16_t s_cloud_div    = 0;

void Cloud_Queue(const char *line)
{
    if (s_cloud_pend_n >= CLOUD_PEND_MAX) return;
    strncpy(s_cloud_pend[s_cloud_pend_n], line, CLOUD_PEND_LEN - 1);
    s_cloud_pend[s_cloud_pend_n][CLOUD_PEND_LEN - 1] = 0;
    s_cloud_pend_n++;
}

static void Cloud_SendStatus(void)
{
    char line[160];
    char snap[80];
    char cbuf[8];
    uint8_t i, o = 0, occ = 0, door = 0;
    CodeEntry e;

    for (i = 1; i <= BOX_COUNT; i++)
    {
        if (CodeBox_GetEntry(i, &e))
        {
            occ |= (uint8_t)(1u << (i - 1));
            memcpy(cbuf, e.code, CODE_LEN);
            cbuf[CODE_LEN] = 0;
        }
        else
        {
            strcpy(cbuf, "------");
        }
        /* 门磁: 1=开. I2C读失败(2)按“关”处理, 避免误报门开 */
        if (Lock_ReadFeedback(i) == 1) door |= (uint8_t)(1u << (i - 1));
        if (i > 1) snap[o++] = '|';
        o += (uint8_t)sprintf(snap + o, "%d:%s", (int)i, cbuf);
    }
    snap[o] = 0;
    sprintf(line, "STATUS %d %d %s", (int)occ, (int)door, snap);
    ESP8266_SendText(line);
    printf("[CLOUD] %s\r\n", line);
}

static void Cloud_RemoteOpen(uint8_t box)
{
    uint8_t idx = (uint8_t)(box - 1);
    s_res_box  = box;
    s_res_ok   = 1;
    s_door_flow[idx]  = 3;
    s_door_src[idx]   = SRC_TOUCH;
    s_door_phase[idx] = DOOR_W_OPEN;
    s_door_open_tick[idx] = loop_tick;
    s_door_db[idx]    = 0;
    DoOpenBox(box, SRC_TOUCH);
    StatusBar_Set(SRC_TOUCH, box, "远程开锁");
    s_overlay   = 0;
    s_chat_mode = 0;
    s_state = ST_PICKUP_DONE;
    PickupDone_Draw();
    printf("[CLOUD] remote open box %d\r\n", (int)box);
}

void Cloud_HandleLine(const char *line)
{
    const char *p;
    uint8_t kind;      /* 1=OPEN/UNLOCK, 2=SET, 3=CLEAR */
    int b = 0;

    if (strncmp(line, "AI:", 3) == 0)
    {
        if (s_chat_mode) Chat_HandleResponse(line);
        return;
    }
    if (strncmp(line, "OPEN ", 5) == 0)        { p = line + 5; kind = 1; }
    else if (strncmp(line, "UNLOCK ", 7) == 0) { p = line + 7; kind = 1; }
    else if (strncmp(line, "SET ", 4) == 0)    { p = line + 4; kind = 2; }
    else if (strncmp(line, "CLEAR ", 6) == 0)  { p = line + 6; kind = 3; }
    else { printf("[CLOUD] unknown downlink: %s\r\n", line); return; }

    while (*p >= '0' && *p <= '9') { b = b * 10 + (*p - '0'); p++; }
    if (b < 1 || b > BOX_COUNT) { printf("[CLOUD] bad box: %s\r\n", line); return; }

    if (kind == 2)
    {
        char code[8];
        uint8_t n = 0, i;
        uint8_t cb[CODE_LEN];
        while (*p == ' ') p++;
        while (*p && *p != ' ' && n < CODE_LEN) code[n++] = *p++;
        code[n] = 0;
        if (n != CODE_LEN) { printf("[CLOUD] SET bad code: %s\r\n", line); return; }
        for (i = 0; i < CODE_LEN; i++) cb[i] = (uint8_t)code[i];
        CodeBox_DirectSet((uint8_t)b, cb);
        printf("[CLOUD] SET box %d code %s\r\n", b, code);
        return;
    }
    if (kind == 3)
    {
        CodeBox_Clear((uint8_t)b);
        printf("[CLOUD] CLEAR box %d\r\n", b);
        return;
    }
    Cloud_RemoteOpen((uint8_t)b);
}

void Cloud_Tick(void)
{
    if (!ESP8266_IsReady())
    {
        s_cloud_was_up = 0;
        return;
    }
    if (!s_cloud_was_up)
    {
        s_cloud_was_up = 1;
        Cloud_Queue("HELLO");
    }
    /* 对话模式优先占用发送缓冲, 别把 CHAT: 上行挤掉 */
    if (s_chat_mode || s_chat_busy) return;

    if (s_cloud_pend_n)
    {
        if (ESP8266_SendText(s_cloud_pend[0]))
        {
            uint8_t i;
            for (i = 1; i < s_cloud_pend_n; i++)
                strcpy(s_cloud_pend[i - 1], s_cloud_pend[i]);
            s_cloud_pend_n--;
        }
        return;
    }

    if (++s_cloud_div >= CLOUD_STATUS_TICKS)
    {
        s_cloud_div = 0;
        Cloud_SendStatus();
    }
}
