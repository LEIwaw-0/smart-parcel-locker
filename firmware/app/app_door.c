/******************************************************
**  app_door.c - lock drive and door-open feedback tracking.
**
**  Split out of the monolithic main.c; bodies are unchanged.
******************************************************/
#include "app_door.h"
#include "app_ctx.h"
#include "app_cloud.h"    /* Cloud_Queue on DEPOSITED / PICKED */
#include "app_ui.h"       /* StatusBar_Set, Menu_Draw */
#include "codebox.h"
#include "delay.h"
#include "lcd.h"
#include "lock.h"
#include "syn6288.h"
#include <stdio.h>

/* ---- private state: the status-band widget cache ---- */
static uint8_t  s_band_phase_cache = 0xFF;
static uint8_t  s_band_fb_cache    = 0xFF;
static uint16_t s_band_y           = 0;

static const char *DoorPromptText(uint16_t *tx)
{
    uint8_t idx   = (uint8_t)(s_res_box - 1);
    uint8_t phase = s_door_phase[idx];
    uint8_t flow  = s_door_flow[idx];
    if (phase == DOOR_W_OPEN)
    {
        *tx = 184;
        return "请取你的物品";
    }
    if (phase == DOOR_W_CLOSE)
    {
        *tx = 154;
        return (flow == 1) ? "门开中，请投件请关门"
                           : "门开中，请取件请关门";
    }
    *tx = 162;
    return (flow == 1) ? "门已关 投件完成"
                       : "门已关 取件完成";
}

void DoorStatus_Update(void)
{
    uint8_t fb, idx;

    if (!s_res_ok || s_res_box < 1 || s_res_box > BOX_COUNT) return;
    idx = (uint8_t)(s_res_box - 1);
    if (s_door_phase[idx] == DOOR_IDLE) return;
    fb = Lock_ReadFeedback(s_res_box);

    if (s_band_phase_cache != s_door_phase[idx])
    {
        const char *t;
        uint16_t tx;
        s_band_phase_cache = s_door_phase[idx];
        LCD_Fill(0, s_band_y, LCD_W - 1, (uint16_t)(s_band_y + 18), COLOR_BLACK);
        t = DoorPromptText(&tx);
        LCD_ShowStringCN(tx, s_band_y, t,
                         (s_door_phase[idx] == DOOR_DONE) ? COLOR_GREEN : COLOR_CYAN,
                         COLOR_BLACK, 2);
    }

    if (fb != 2 && s_band_fb_cache != fb)
    {
        const char *t = fb ? "状态:" : "状态:开";
        s_band_fb_cache = fb;
        LCD_Fill(0, (uint16_t)(s_band_y + 26), LCD_W - 1, (uint16_t)(s_band_y + 46), COLOR_BLACK);
        LCD_ShowStringCN(194, (uint16_t)(s_band_y + 28), t,
                         COLOR_YELLOW, COLOR_BLACK, 2);
    }
}

void DoorStatus_Draw(uint16_t y)
{
    s_band_y           = y;
    s_band_phase_cache = 0xFF;
    s_band_fb_cache    = 0xFF;
    LCD_Fill(0, y, LCD_W - 1, (uint16_t)(y + 58), COLOR_BLACK);
    DoorStatus_Update();
}

void DoOpenBox(uint8_t box_id, uint8_t src)
{
    LCD_Backlight(0);
    delay_ms(50);
    Lock_Open(box_id);
    delay_ms(80);           /* let relay contact + 5V rail settle */
    LCD_Init();             /* recover LCD controller from relay EMI */
    (void)src;

}

void Door_Tick(void)
{
    uint8_t box;
    for (box = 1; box <= BOX_COUNT; box++)
    {
        uint8_t idx = (uint8_t)(box - 1);
        uint8_t phase = s_door_phase[idx];
        uint8_t fb;

        if (phase == DOOR_IDLE || phase == DOOR_DONE) continue;

        fb = Lock_ReadFeedback(box);
        if (fb == 2) continue;   /* I2C failure: skip this box */

        /* 30s safety net (pickup only): if the door never closes,
           treat the item as taken and release the box. */
        if (s_door_flow[idx] == 2 &&
            (uint32_t)(loop_tick - s_door_open_tick[idx]) >= PICKUP_TIMEOUT_TICKS)
        {
            s_door_phase[idx] = DOOR_DONE;
            s_door_db[idx]    = 0;
            CodeBox_Clear(box);   /* release the box: item assumed taken */
            {
                char cl[CLOUD_PEND_LEN];
                sprintf(cl, "PICKED %d", (int)box);
                Cloud_Queue(cl);
            }

            if (s_door_flow[idx] == 1)
            {
                SYN_FrameInfo(0, (uint8_t *)"[v8][t5][m0]投件完成");
                StatusBar_Set(s_door_src[idx], box, "投件完成");
            }
            else
            {
                SYN_FrameInfo(0, (uint8_t *)"[v8][t5][m0]取件完成");
                StatusBar_Set(s_door_src[idx], box, "超时已完成");
            }
            printf("[DOOR] box %d timeout, flow %d complete\r\n", box, s_door_flow[idx]);
            if (s_res_box == box &&
                (s_state == ST_PICKUP_DONE || s_state == ST_DEPOSIT_DONE))
            {
                s_state = ST_MENU;
                Menu_Draw();
            }
            continue;
        }

        if (phase == DOOR_W_OPEN)
        {
            if (fb == 1) { if (++s_door_db[idx] >= 2) { s_door_db[idx] = 0; s_door_phase[idx] = DOOR_W_CLOSE; } }
            else         { s_door_db[idx] = 0; }
        }
        else /* DOOR_W_CLOSE */
        {
            if (fb == 0)
            {
                if (++s_door_db[idx] >= 3)
                {
                    s_door_db[idx] = 0;
                    s_door_phase[idx] = DOOR_DONE;
                    if (s_door_flow[idx] == 2)
                        CodeBox_Clear(box);   /* pickup: release box only after door closed */
                    if (s_door_flow[idx] == 1)
                    {
                        char cl[CLOUD_PEND_LEN];
                        sprintf(cl, "DEPOSITED %d", (int)box);
                        Cloud_Queue(cl);
                        SYN_FrameInfo(0, (uint8_t *)"[v8][t5][m0]投件完成");
                        StatusBar_Set(s_door_src[idx], box, "投件完成");
                    }
                    else if (s_door_flow[idx] == 2)
                    {
                        char cl[CLOUD_PEND_LEN];
                        sprintf(cl, "PICKED %d", (int)box);
                        Cloud_Queue(cl);
                        SYN_FrameInfo(0, (uint8_t *)"[v8][t5][m0]取件完成");
                        StatusBar_Set(s_door_src[idx], box, "取件完成");
                    }
                    else
                    {
                        SYN_FrameInfo(0, (uint8_t *)"[v8][t5][m0]门已关");
                        StatusBar_Set(s_door_src[idx], box, "已关门");
                    }

                    printf("[DOOR] box %d closed, flow %d complete\r\n", box, s_door_flow[idx]);
                    if (s_res_box == box &&
                        (s_state == ST_PICKUP_DONE || s_state == ST_DEPOSIT_DONE))
                    {
                        s_state = ST_MENU;
                        Menu_Draw();
                    }
                }
            }
            else { s_door_db[idx] = 0; }
        }
    }
}
