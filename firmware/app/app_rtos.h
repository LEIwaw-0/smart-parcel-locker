#ifndef __APP_RTOS_H
#define __APP_RTOS_H

/******************************************************
**  app_rtos.h - the event/IPC contract between the tasks
**
**  Who owns what (one writer, and only that writer):
**
**      tsk_ui     LCD, s_state, the UI results/keypad/chat globals,
**                 CodeBox (Flash), SYN6288 (TTS), the business rules
**      tsk_touch  the touch panel (PA0/PA1/PE7-9)
**      tsk_voice  LD3320 (USART2)
**      tsk_door   the four relays, the PCF8574 (PB6/PB7), the per-box
**                 door tracker (phase / flow / src / open time)
**      tsk_link   ESP8266 (USART3), every AT/TCP call, the AT state machine
**      tsk_clock  the wall clock and the weather data (borrows the link)
**
**  Everything the old superloop did with a shared variable is now either
**  an event on q_ui, or one of the small read-only snapshots below.
**
**  The snapshots are deliberate: a single byte, written by exactly one
**  task and only ever compared by the readers.  That is not "sharing a
**  structure", it is a status lamp, and it needs no lock.
******************************************************/
#include "app_config.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

/* ---- event payload ---- */
#define APP_EV_TEXT_MAX   96

enum {
    EV_TOUCH = 1,       /* a = td_held(1 按下 / 0 释放), x,y = 屏幕坐标      */
    EV_VOICE,           /* a = VS_*, b/c = 载荷, text = 取件码/原始行       */
    EV_CLOUD_LINE,      /* text = 云端下行原始行                            */
    EV_DOOR_STATE,      /* a = box, b = phase, c = flow                     */
    EV_DOOR_DONE        /* a = box, b = flow, c = src, x = 1 超时 / 0 关门  */
};

/* EV_VOICE 的 a 字段：语音事件的种类 */
enum {
    VS_DIGIT = 1,       /* b = 位序 0..5, c = 数字值                         */
    VS_UNDO,
    VS_PENDING,         /* text = 6 位码，等用户说"对/错"                   */
    VS_RETRY,
    VS_CODE,            /* text = 6 位码，确认取件                          */
    VS_CHAT_WAKE,       /* 进入小智对话模式                                 */
    VS_CHAT,            /* b = 对话关键词编码                               */
    VS_CMD,             /* b = LD3320 CMD 码：0x00=唤醒词 0xFE=超时 其它只打印 */
    VS_RAW              /* text = LD3320 原始行（串口已回显，这里只决定提示行） */
};

typedef struct {
    uint8_t  kind;
    uint8_t  a;
    uint8_t  b;
    uint8_t  c;
    uint16_t x;
    uint16_t y;
    char     text[APP_EV_TEXT_MAX];
} app_evt_t;

/* ---- q_link 上的上行种类 ---- */
enum {
    LK_LINE = 1,        /* text = 要发的一行（HELLO / STATUS / DEPOSITED / CHAT:） */
    LK_FETCH            /* 请求 link 抓一次天气（借用链路做 HTTP GET）             */
};

typedef struct {
    uint8_t kind;
    char    text[APP_EV_TEXT_MAX];
} app_link_t;

/* ---- IPC objects (created by App_RtosStart, before the scheduler) ---- */
extern QueueHandle_t     q_ui;      /* 8 x app_evt_t   各任务 -> UI           */
extern QueueHandle_t     q_link;    /* CLOUD_PEND_MAX x app_link_t  UI -> link */
extern QueueHandle_t     q_clock;   /* 2 x uint8        link -> clock         */
extern QueueHandle_t     q_door;    /* BOX_COUNT x uint8  UI -> door（开箱请求）*/
extern SemaphoreHandle_t m_lcd;     /* LCD 绘制互斥（UI 与 clock 两个写者）    */

/* ---- 跨任务只读快照：单字节、写者唯一、读者只判断 ---- */
extern volatile uint8_t g_link_status;  /* ESP8266_GetStatus() 状态码 0..11     */
extern volatile uint8_t g_link_up;      /* 1 = TCP 已建立，可以发送             */
extern volatile uint8_t g_link_fetch;   /* 1 = 正在借用链路抓天气（画面徽标用） */
extern volatile uint8_t g_door_fb;      /* bit(i-1) = 第 i 箱门磁为"开"         */
extern volatile uint8_t g_door_fb_ok;   /* 0 = 最近一次 PCF8574 读取失败       */
extern volatile uint8_t g_lock_mask;    /* 当前吸合的继电器掩码                 */
extern volatile uint8_t g_ui_screen;    /* 1 = 菜单且无遮罩（卡片可见）         */
extern volatile uint8_t g_chat_busy;    /* 1 = 对话中：别抢链路发送缓冲         */
extern volatile uint8_t g_voice_seen;   /* 1 = LD3320 有过任何输出              */

/* ---- 启动与公共工具 ---- */
void     App_RtosStart(void);           /* 建 IPC + 建任务 + 启动调度器 */
uint32_t App_Ms(void);                  /* 毫秒级时基（= tick 计数），取代 loop_tick */

/* LCD 互斥：调度器启动前直接放行（那时只有一条执行流） */
uint8_t  Lcd_Lock(void);
void     Lcd_Unlock(void);

/* ---- 投递 API（全部非阻塞：满了就丢并计数，绝不把生产者卡住）---- */
void App_UiPost(const app_evt_t *ev);
void App_LinkPost(const char *line);
void App_FetchPost(void);
void App_DoorPost(uint8_t box, uint8_t flow, uint8_t src);

/* ---- 任务体，各自实现在自己的模块里 ---- */
void Ui_Task(void *arg);
void Touch_Task(void *arg);
void Voice_Task(void *arg);
void Door_Task(void *arg);
void Link_Task(void *arg);
void Clock_Task(void *arg);

#endif /* __APP_RTOS_H */
