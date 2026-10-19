/******************************************************
**  app_rtos.c - IPC objects, task creation, diagnostics.
**
**  Called once from main() after the peripheral bring-up, before
**  vTaskStartScheduler().  Two things happen here:
**
**    1. the queues and the LCD mutex are created (still single-threaded,
**       so this is the only moment they can be created without races)
**    2. the six worker tasks and the diagnostics task are created, and
**       the scheduler is started - main() never returns
**
**  Stack sizes are in words, not bytes (4KB ui = 1024).  The ui task is
**  the big one because it carries printf/sprintf chains, the Flash code
**  table and the whole LCD path.
******************************************************/
#include "app_rtos.h"
#include "app_ctx.h"
#include "hooks.h"
#include "usart.h"
#include "lock.h"
#include <stdio.h>

/* ---- the IPC objects ---- */
QueueHandle_t     q_ui    = NULL;
QueueHandle_t     q_link  = NULL;
QueueHandle_t     q_clock = NULL;
QueueHandle_t     q_door  = NULL;
SemaphoreHandle_t m_lcd   = NULL;

/* ---- read-only snapshots, see app_rtos.h ---- */
volatile uint8_t g_link_status = 0;
volatile uint8_t g_link_up     = 0;
volatile uint8_t g_link_fetch  = 0;
volatile uint8_t g_door_fb     = 0;
volatile uint8_t g_door_fb_ok  = 0;
volatile uint8_t g_lock_mask   = 0;
volatile uint8_t g_ui_screen   = 0;
volatile uint8_t g_chat_busy   = 0;
volatile uint8_t g_voice_seen  = 0;

/* ---- diagnostics: dropped event counters (a full queue is a design bug) ---- */
static uint32_t s_drop_ui   = 0;
static uint32_t s_drop_link = 0;
static uint32_t s_drop_door = 0;

/* ---- task table for the diag task's stack water marks ---- */
typedef struct {
    const char    *name;
    TaskHandle_t   handle;
    unsigned short stack_words;
} task_slot_t;

static task_slot_t s_slots[7];
static uint8_t     s_slot_n = 0;

uint32_t App_Ms(void)
{
    return (uint32_t)xTaskGetTickCount();
}

uint8_t Lcd_Lock(void)
{
    /* 调度器没跑就没人跟 UI 抢屏幕，直接放行。
       注意：xSemaphoreTake 在调度器启动前调用是非法操作。 */
    if (xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED) return 1;
    return (xSemaphoreTake(m_lcd, portMAX_DELAY) == pdTRUE) ? 1 : 0;
}

void Lcd_Unlock(void)
{
    if (xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED) return;
    xSemaphoreGive(m_lcd);
}

void App_UiPost(const app_evt_t *ev)
{
    if (xQueueSend(q_ui, ev, 0) != pdTRUE)
    {
        s_drop_ui++;      /* UI 是最高优先级任务，队列满说明它自己卡在别处 */
    }
}

void App_LinkPost(const char *line)
{
    app_link_t item;
    uint8_t i;

    item.kind = LK_LINE;
    for (i = 0; i < (APP_EV_TEXT_MAX - 1) && line[i]; i++)
        item.text[i] = line[i];
    item.text[i] = 0;

    if (xQueueSend(q_link, &item, 0) != pdTRUE)
    {
        s_drop_link++;
        printf("[LINK] uplink queue full, dropped: %s\r\n", line);
    }
}

void App_FetchPost(void)
{
    app_link_t item;
    item.kind  = LK_FETCH;
    item.text[0] = 0;
    if (xQueueSend(q_link, &item, 0) != pdTRUE)
    {
        s_drop_link++;
        printf("[LINK] uplink queue full, weather request dropped\r\n");
    }
}

void App_DoorPost(uint8_t box, uint8_t flow, uint8_t src)
{
    uint8_t req = (uint8_t)((box & 0x0Fu) | (flow << 4) | (src << 6));
    if (xQueueSend(q_door, &req, 0) != pdTRUE)
    {
        s_drop_door++;
        printf("[DOOR] open request queue full, box %d dropped\r\n", (int)box);
    }
}

/*------------------------------------------------------
 * 诊断任务：每 5s 报一次堆水位 / 各任务栈高水位 / 继电器掩码
 * 这几行是"多任务跑起来之后仍然健康"的唯一自动证据。
 *------------------------------------------------------*/
static void Diag_Task(void *arg)
{
    uint8_t i;
    (void)arg;

    for (;;)
    {
        vTaskDelay(pdMS_TO_TICKS(5000));

        printf("[DIAG] heap free=%lu min-ever=%lu  lock mask=0x%X  drops ui/link/door=%lu/%lu/%lu\r\n",
               (unsigned long)xPortGetFreeHeapSize(),
               (unsigned long)xPortGetMinimumEverFreeHeapSize(),
               (unsigned)g_lock_mask,
               (unsigned long)s_drop_ui, (unsigned long)s_drop_link, (unsigned long)s_drop_door);
        for (i = 0; i < s_slot_n; i++)
        {
            printf("[DIAG] task %-6s stack left=%u words (of %u)\r\n",
                   s_slots[i].name,
                   (unsigned)uxTaskGetStackHighWaterMark(s_slots[i].handle),
                   (unsigned)s_slots[i].stack_words);
        }
    }
}

static void prvCreate(const char *name, TaskFunction_t fn, unsigned short stack,
                      UBaseType_t prio)
{
    TaskHandle_t h = NULL;
    if (xTaskCreate(fn, name, stack, NULL, prio, &h) != pdPASS)
    {
        /* 栈或 TCB 分配失败：直接用裸串口喊，然后停机（此刻内核细节不可信） */
        vAssertCalled(__FILE__, __LINE__);
    }
    if (s_slot_n < (uint8_t)(sizeof(s_slots) / sizeof(s_slots[0])))
    {
        s_slots[s_slot_n].name        = name;
        s_slots[s_slot_n].handle      = h;
        s_slots[s_slot_n].stack_words = stack;
        s_slot_n++;
    }
}

void App_RtosStart(void)
{
    /* ---- 1) IPC：队列一律"非阻塞发送"，任何投递失败都不许卡住生产者 ---- */
    q_ui    = xQueueCreate(8, sizeof(app_evt_t));
    q_link  = xQueueCreate(CLOUD_PEND_MAX, sizeof(app_link_t));
    q_clock = xQueueCreate(2, sizeof(uint8_t));
    q_door  = xQueueCreate(BOX_COUNT, sizeof(uint8_t));
    m_lcd   = xSemaphoreCreateMutex();

    if (q_ui == NULL || q_link == NULL || q_clock == NULL ||
        q_door == NULL || m_lcd == NULL)
    {
        vAssertCalled(__FILE__, __LINE__);      /* 堆不够建对象 */
    }

    /* ---- 2) 任务。优先级：UI > 触摸/语音/门控 > 链路 > 时钟 > 诊断 > IDLE ---- */
    prvCreate("ui",    Ui_Task,    1024, 5);
    prvCreate("touch", Touch_Task,  256, 4);
    prvCreate("voice", Voice_Task,  384, 4);
    prvCreate("door",  Door_Task,   384, 4);
    prvCreate("link",  Link_Task,   768, 3);
    prvCreate("clock", Clock_Task,  320, 2);
    prvCreate("diag",  Diag_Task,   320, 1);

    g_sched_started = 1;

    printf("[RTOS] tasks up: ui/touch/voice/door/link/clock/diag, heap free=%lu\r\n\r\n",
           (unsigned long)xPortGetFreeHeapSize());

    vTaskStartScheduler();
    /* 只有堆耗尽才会走到这里 */
}
