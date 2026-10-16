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
#define PICKUP_TIMEOUT_TICKS  6000u   /* 30s at ~5ms per loop */

/* ---- voice ---- */
#define VOICE_DEDUP_TICKS  1600u      /* 1600 x 5ms = 8s */
#define CHAT_IDLE_TICKS    5000u      /* 25s: STC11 deaf-window + listen + reaction */

/* ---- wall clock / weather over the borrowed ESP8266 link ----
   No RTC on the board: the clock is seeded from the HTTP "Date:"
   header and then kept by the free-running DWT cycle counter (168MHz). */
#define WX_HOST            "d1.weather.com.cn"
#define WX_PATH            "/sk_2d/101270107.html"

#define WX_FIRST_TICKS      1200u     /* first sync ~6s after boot  */
#define WX_REFRESH_TICKS   36000u     /* re-sync every ~180s        */
#define WX_RETRY_TICKS      2400u     /* retry after failure (~12s) */
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
#define TITLE_HOLD_TICKS  600u  /* 3s at ~5ms per loop */

/* ---- cloud link (TCP 8090) ----
   Uplink:   HELLO / STATUS <occ_mask> <door_mask> <code snapshots>
             DEPOSITED n / PICKED n
   Downlink: OPEN n / UNLOCK n, SET n <code6>, CLEAR n, AI:<text>
   Door_Tick() runs when the link may be busy, so messages are queued
   and Cloud_Tick() drains the queue once the link is idle again. */
#define CLOUD_PEND_MAX      4
#define CLOUD_PEND_LEN      24
#define CLOUD_STATUS_TICKS  400u   /* 400 x 5ms = 2s */

#endif /* __APP_CONFIG_H */
