/******************************************************
**  app_door.c - lock drive and door-open feedback tracking.
**
**  The door task is the only writer of the relay state and of the per-box
**  tracker (phase / flow / src / open time / debounce counter).  The two
**  things the screen needs are published as events: EV_DOOR_STATE while a
**  box waits for its door to close, EV_DOOR_DONE when the exchange is over
**  (and then the UI task - which owns the status bar, the TTS and the state
**  machine - finishes the flow, exactly like the old superloop did).
**
**  The PCF8574 feedback nibble is published as the read-only g_door_fb /
**  g_door_fb_ok snapshot, used by the status band and by Cloud_SendStatus.
******************************************************/
#include "app_door.h"
#include "app_ctx.h"
#include "app_rtos.h"
#include "delay.h"
#include "lcd.h"
#include "lock.h"
#include "pcf8574.h"
#include <stdio.h>

/* ---- the real tracker: written by the door task only ---- */
static uint8_t  s_phase[BOX_COUNT];
static uint8_t  s_flow[BOX_COUNT];      /* 1 = deposit, 2 = pickup, 3 = remote */
static uint8_t  s_src[BOX_COUNT];       /* status-bar source tag               */
static uint8_t  s_db[BOX_COUNT];        /* stable-reading counter              */
static uint32_t s_open_ms[BOX_COUNT];   /* App_Ms() when the box opened        */

/* ---- the UI-side shadow of the same tracker (updated from the events) ---- */
static uint8_t  s_shadow_phase[BOX_COUNT];
static uint8_t  s_shadow_flow[BOX_COUNT];

/* ---- private state: the status-band widget cache ---- */
static uint8_t  s_band_phase_cache = 0xFF;
static uint8_t  s_band_fb_cache    = 0xFF;
static uint16_t s_band_y           = 0;

void Door_ShadowSet(uint8_t box, uint8_t phase, uint8_t flow)
{
    if (box < 1 || box > BOX_COUNT) return;
    s_shadow_phase[box - 1] = phase;
    s_shadow_flow[box - 1]  = flow;
}

static void Door_PostEvt(uint8_t kind, uint8_t a, uint8_t b, uint8_t c, uint16_t x)
{
    app_evt_t ev;
    ev.kind    = kind;
    ev.a       = a;
    ev.b       = b;
    ev.c       = c;
    ev.x       = x;
    ev.y       = 0;
    ev.text[0] = 0;
    App_UiPost(&ev);
}

static const char *DoorPromptText(uint16_t *tx)
{
    uint8_t idx   = (uint8_t)(s_res_box - 1);
    uint8_t phase = s_shadow_phase[idx];
    uint8_t flow  = s_shadow_flow[idx];
    if (phase == DOOR_W_OPEN)
    {
        *tx = 184;
        return "\xC7\xEB\xC8\xA1\xC4\xE3\xB5\xC4\xCE\xEF\xC6\xB7";
    }
    if (phase == DOOR_W_CLOSE)
    {
        *tx = 154;
        return (flow == 1) ? "\xC3\xC5\xBF\xAA\xD6\xD0\xA3\xAC\xC7\xEB\xCD\xB6\xBC\xFE\xC7\xEB\xB9\xD8\xC3\xC5"
                           : "\xC3\xC5\xBF\xAA\xD6\xD0\xA3\xAC\xC7\xEB\xC8\xA1\xBC\xFE\xC7\xEB\xB9\xD8\xC3\xC5";
    }
    *tx = 162;
    return (flow == 1) ? "\xC3\xC5\xD2\xD1\xB9\xD8 \xCD\xB6\xBC\xFE\xCD\xEA\xB3\xC9"
                       : "\xC3\xC5\xD2\xD1\xB9\xD8 \xC8\xA1\xBC\xFE\xCD\xEA\xB3\xC9";
}

void DoorStatus_Update(void)
{
    uint8_t fb, idx;

    if (!s_res_ok) return;
    if (s_res_box < 1 || s_res_box > BOX_COUNT) return;
    idx = (uint8_t)(s_res_box - 1);
    if (s_shadow_phase[idx] == DOOR_IDLE) return;

    /* the door task publishes the PCF8574 P4-P7 nibble into g_door_fb;
       g_door_fb_ok==0 means the last read failed, which is the old
       "Lock_ReadFeedback() == 2" case: draw the phase line, skip the FB line */
    fb = (uint8_t)((g_door_fb >> idx) & 1u);

    if (s_band_phase_cache != s_shadow_phase[idx])
    {
        const char *t;
        uint16_t tx;
        s_band_phase_cache = s_shadow_phase[idx];
        LCD_Fill(0, s_band_y, LCD_W - 1, (uint16_t)(s_band_y + 18), COLOR_BLACK);
        t = DoorPromptText(&tx);
        LCD_ShowStringCN(tx, s_band_y, t,
                         (s_shadow_phase[idx] == DOOR_DONE) ? COLOR_GREEN : COLOR_CYAN,
                         COLOR_BLACK, 2);
    }

    if (!g_door_fb_ok) return;

    if (s_band_fb_cache != fb)
    {
        const char *t = fb ? "\xD7\xB4\xCC\xAC:" : "\xD7\xB4\xCC\xAC:\xBF\xAA";
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

/******************************************************
**  UI-side open request.
**
**  The shadow moves first: DepositDone_Draw()/PickupDone_Draw() paint the
**  band immediately after this call and must already show WAIT-OPEN.
**  The relay itself is driven by the door task, which is unblocked by the
**  q_door post; it runs during the 80ms vTaskDelay below (door prio 4 < UI
**  prio 5, so the UI task really does yield here).
******************************************************/
void DoOpenBox(uint8_t box_id, uint8_t src, uint8_t flow)
{
    Door_ShadowSet(box_id, DOOR_W_OPEN, flow);

    LCD_Backlight(0);
    delay_ms(50);
    App_DoorPost(box_id, flow, src);
    delay_ms(80);              /* let the relay contact + 5V rail settle */
    LCD_Init();                /* recover the LCD controller from relay EMI */
}

/******************************************************
**  per-box tracking: WAIT-OPEN -> WAIT-CLOSE -> DONE
******************************************************/
static void Door_Advance(void)
{
    uint8_t box;

    for (box = 1; box <= BOX_COUNT; box++)
    {
        uint8_t idx   = (uint8_t)(box - 1);
        uint8_t phase = s_phase[idx];
        uint8_t fb;

        if (phase == DOOR_IDLE || phase == DOOR_DONE) continue;

        fb = Lock_ReadFeedback(box);
        if (fb == 2) continue;      /* I2C failure: skip this box */

        /* 30s safety net (pickup only): if the door never closes, treat the
           item as taken and release the box */
        if (s_flow[idx] == 2 &&
            (uint32_t)(App_Ms() - s_open_ms[idx]) >= PICKUP_TIMEOUT_MS)
        {
            s_phase[idx] = DOOR_DONE;
            s_db[idx]    = 0;
            Door_PostEvt(EV_DOOR_DONE, box, s_flow[idx], s_src[idx], 1);
            continue;
        }

        if (phase == DOOR_W_OPEN)
        {
            if (fb == 1)
            {
                if (++s_db[idx] >= 2)
                {
                    s_db[idx]    = 0;
                    s_phase[idx] = DOOR_W_CLOSE;
                    Door_PostEvt(EV_DOOR_STATE, box, DOOR_W_CLOSE, s_flow[idx], 0);
                }
            }
            else s_db[idx] = 0;
        }
        else /* DOOR_W_CLOSE */
        {
            if (fb == 0)
            {
                if (++s_db[idx] >= 3)
                {
                    s_db[idx]    = 0;
                    s_phase[idx] = DOOR_DONE;
                    Door_PostEvt(EV_DOOR_DONE, box, s_flow[idx], s_src[idx], 0);
                }
            }
            else s_db[idx] = 0;
        }
    }
}

/******************************************************
**  the door task
**
**  One 20ms cadence drives everything: the relay auto-off watchdog (the
**  solenoid must never stay energised beyond LOCK_ON_TIME_MS), the relay
**  hardware heartbeat, the 200ms PCF8574 feedback poll that feeds the
**  tracker, and the open requests arriving on q_door.
******************************************************/
void Door_Task(void *arg)
{
    uint32_t next_fb_ms = 0;
    uint32_t hb_ms      = 0;
    uint8_t  prev_fbnib = 0xFF;
    uint8_t  fb_first   = 1;
    uint8_t  req;

    (void)arg;

    for (;;)
    {
        uint32_t now = App_Ms();

        /* 1) relay auto-off: the 2s solenoid protection */
        Lock_Tick();
        g_lock_mask = Lock_GetActiveMask();

        /* 1b) heartbeat: while any relay is on, log the mask every 200ms */
        if (Lock_IsOpen(1) || Lock_IsOpen(2) || Lock_IsOpen(3) || Lock_IsOpen(4))
        {
            if ((uint32_t)(now - hb_ms) >= 200u)
            {
                uint8_t exp_mask = Lock_GetActiveMask();
                uint8_t rb_mask  = Lock_HW_ReadBack();
                hb_ms = now;
                if (rb_mask == 0xFF)
                    printf("HB: %lums exp=0x%X readback fail\r\n", (unsigned long)now, exp_mask);
                else
                    printf("HB: %lums exp=0x%X real=0x%X %s\r\n", (unsigned long)now, exp_mask, rb_mask,
                           (rb_mask == exp_mask) ? "ok" : "MISMATCH");
            }
        }
        else
        {
            hb_ms = now;
        }

        /* 2) PCF8574 P4-P7 poll + door tracking, 200ms */
        if ((int32_t)(now - next_fb_ms) >= 0)
        {
            uint8_t fraw;
            next_fb_ms = now + DOOR_FB_POLL_MS;
            if (PCF8574_ReadByte(&fraw) == 0)
            {
                uint8_t nib = (uint8_t)((fraw >> 4) & 0x0F);
                g_door_fb    = nib;          /* bit(i-1) = box i open */
                g_door_fb_ok = 1;
                if (fb_first || nib != prev_fbnib)
                {
                    printf("FB P7..P4=%d%d%d%d\r\n",
                           (nib >> 3) & 1, (nib >> 2) & 1, (nib >> 1) & 1, nib & 1);
                    prev_fbnib = nib;
                    fb_first   = 0;
                }
            }
            else
            {
                g_door_fb_ok = 0;
            }
            Door_Advance();
        }

        /* 3) one open request, or just the 20ms watchdog tick */
        if (xQueueReceive(q_door, &req, pdMS_TO_TICKS(DOOR_LOCK_MS)) == pdTRUE)
        {
            uint8_t box  = (uint8_t)(req & 0x0Fu);
            uint8_t flow = (uint8_t)((req >> 4) & 0x03u);
            uint8_t src  = (uint8_t)((req >> 6) & 0x03u);

            if (box >= 1 && box <= BOX_COUNT)
            {
                uint8_t idx = (uint8_t)(box - 1);
                s_flow[idx]    = flow;
                s_src[idx]     = src;
                s_phase[idx]   = DOOR_W_OPEN;
                s_open_ms[idx] = App_Ms();
                s_db[idx]      = 0;
                Lock_Open(box);
                g_lock_mask = Lock_GetActiveMask();
                printf("[DOOR] box %d open (flow %d src %d)\r\n",
                       (int)box, (int)flow, (int)src);
                Door_PostEvt(EV_DOOR_STATE, box, DOOR_W_OPEN, flow, 0);
            }
        }
    }
}
