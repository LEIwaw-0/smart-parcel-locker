#ifndef __APP_CONFIG_H
#define __APP_CONFIG_H

/******************************************************
**  app_config.h - project-wide constants
**
**  Everything here used to sit at the top of a single
**  2266-line main.c.  Splitting that file into modules
**  would otherwise have duplicated these values, so the
**  truly global ones (geometry, state codes, timings and
**  the main-screen layout) are collected here.
******************************************************/
#include "stm32f4xx.h"
#include "lcd.h"          /* COLOR_* palette used by the layout macros */

/* ---- cabinet geometry ---- */
#define BOX_COUNT          4

/* ---- status bar (bottom of the 320x480 panel) ---- */
#define STATUS_BAR_Y       296

/* ---- operation source tag, shown in the status bar ---- */
#define SRC_TOUCH         1
#define SRC_VOICE         2

/* ---- UI state machine ---- */
enum {
    ST_MENU = 0,          /* main menu: DEPOSIT / PICKUP                 */
    ST_DEPOSIT_KEYPAD,    /* entering 6-digit phone tail                 */
    ST_DEPOSIT_DONE,      /* show box + code, box open, wait BACK        */
    ST_PICKUP_KEYPAD,     /* entering 6-digit pickup code                */
    ST_PICKUP_DONE,       /* show pickup result, wait BACK               */
    ST_ADMIN              /* admin mode: view all codes                  */
};

/* ---- door-open tracking via the PCF8574 P4-P7 feedback ----
   Lock_ReadFeedback(id) -> 1 = open, 0 = closed, 2 = I2C failure.
   After a box opens: WAIT-OPEN -> WAIT-CLOSE -> DONE.        */
#define DOOR_IDLE     0
#define DOOR_W_OPEN   1
#define DOOR_W_CLOSE  2
#define DOOR_DONE     3
#define PICKUP_TIMEOUT_MS   30000u   /* 30s: box left ajar -> item assumed taken */

/* ---- touch task cadence ----
   5ms   : raw sample + debounce (the driver's own timing)
   50ms  : a held finger is re-announced this often, so the title-bar
           long press and a finger sliding off a button are still seen */
#define TOUCH_SAMPLE_MS     5u
#define TOUCH_REPEAT_MS     50u

/* ---- door task cadence ----
   20ms  : relay auto-off watchdog (Lock_Tick, 2s solenoid protection)
   200ms : PCF8574 door-feedback poll + phase advance (was 40 x 5ms)   */
#define DOOR_LOCK_MS        20u
#define DOOR_FB_POLL_MS     200u

/* ---- voice ----
   All timings are milliseconds now: the old "x 5ms per loop" counters are
   gone with the superloop, everything runs off App_Ms() / vTaskDelay().   */
#define VOICE_DEDUP_MS      8000u    /* same code twice within 8s = a repeat */
#define CHAT_IDLE_MS        25000u   /* 25s: STC11 deaf-window + listen + reaction */
#define VOX_HINT_MS         2000u    /* how long a voice hint stays on the menu */
#define VOX_HINT_LONG_MS    5000u    /* digit-capture progress stays longer */

/* ---- wall clock / weather over the borrowed ESP8266 link ----
   No RTC on the board: the clock is seeded from the HTTP "Date:"
   header and then kept by the free-running DWT cycle counter (168MHz). */
#define WX_HOST            "d1.weather.com.cn"
#define WX_PATH            "/sk_2d/101270107.html"

#define WX_FIRST_MS         6000u     /* first sync ~6s after boot   */
#define WX_REFRESH_MS      180000u    /* re-sync every ~180s         */
#define WX_RETRY_MS         12000u    /* retry after failure (~12s)  */
#define CLK_CYC_PER_MS     168000u    /* DWT cycles per millisecond */
#define CLK_CYC_PER_SEC    (CLK_CYC_PER_MS * 1000u)

/* ---- main-screen palette and layout ---- */
#define UI_TITLE_TOP   0x2C7F
#define UI_TITLE_BOT   0x1C9F
#define UI_TITLE_MID   0x248F   /* gradient colour behind the title glyphs */
#define UI_CARD_BG     COLOR_PANEL
#define UI_CARD_LINE   0x4208
#define CLK_X1    10
#define CLK_Y1    42
#define CLK_X2   236
#define CLK_Y2   190
#define WX_X1    244
#define WX_Y1     42
#define WX_X2    470
#define WX_Y2    190
#define PICK_X1  100
#define PICK_Y1  200
#define PICK_X2  380
#define PICK_Y2  270
#define TITLE_HOLD_MS     3000u   /* hold the title bar this long for admin */

/* ---- cloud link (TCP 8090) ----
   Uplink:   HELLO / STATUS <occ_mask> <door_mask> <code snapshots>
             DEPOSITED n / PICKED n
   Downlink: OPEN n / UNLOCK n, SET n <code6>, CLEAR n, AI:<text>
   Every uplink line (HELLO / STATUS / DEPOSITED / PICKED / CHAT:) is posted
   to q_link, and the link task sends it once the ESP8266 is idle again. */
#define CLOUD_PEND_MAX      4
#define CLOUD_PEND_LEN      24
#define CLOUD_STATUS_MS     2000u   /* push one STATUS line this often */

/* ---- UI task housekeeping cadence ----
   The UI task blocks on its event queue; the timeout only exists so the
   deadline checks (chat idle, hint expiry, the 2s STATUS push, the 200ms
   DONE-screen band) still get serviced.  Nothing is polled here. */
#define UI_TICK_MS     50u

#endif /* __APP_CONFIG_H */
