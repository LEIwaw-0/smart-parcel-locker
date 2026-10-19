/******************************************************
**  app_ui_task.c - the UI task: event dispatch + deadlines.
**
**  This is the old superloop, turned inside out.  Instead of polling the
**  touch panel, the voice board, the cloud and the door feedback every 5ms,
**  the task blocks on q_ui and reacts to what actually happened:
**
**      EV_TOUCH       -> run the current screen's State_*(td_held, lx, ly)
**      EV_VOICE       -> keypad injection / pickup code / wake / CMD
**      EV_CLOUD_LINE  -> Cloud_HandleLine()
**      EV_DOOR_STATE  -> move the door shadow, repaint the status band
**      EV_DOOR_DONE   -> finish the deposit / pickup flow (code table,
**                        TTS, status bar, back to the menu)
**
**  The queue receive has a timeout, but it is NOT a poll: it only exists so
**  the three deadlines below still get serviced -
**
**      chat idle 25s       -> leave chat mode
**      voice hint 2s/5s    -> put the menu hint line back to standby
**      cloud STATUS 2s     -> one uplink line, if the link is idle
**      door band 200ms     -> repaint the band while a DONE screen is up
**
**  The UI task is the only writer of the LCD apart from the clock task, so
**  every path that draws runs under m_lcd (lock order: lcd -> log).
******************************************************/
#include "app_rtos.h"
#include "app_ctx.h"
#include "app_ui.h"
#include "app_flow.h"
#include "app_voice.h"
#include "app_cloud.h"
#include "app_door.h"
#include "codebox.h"
#include "keypad.h"
#include "ld3320.h"
#include "lcd.h"
#include "syn6288.h"
#include <stdio.h>
#include <string.h>

/* ---- private state: the menu hint's lifetime (was s_vox_flash) ---- */
static uint8_t  s_vox_active = 0;
static uint32_t s_vox_until  = 0;

static void Ui_HintStart(uint32_t ms)
{
    s_vox_active = 1;
    s_vox_until  = App_Ms() + ms;
}

static void Ui_HintCancel(void)
{
    s_vox_active = 0;
}

/******************************************************
**  one touch sample, replayed into the state machine
******************************************************/
static void Ui_Touch(uint8_t td_held, uint16_t lx, uint16_t ly)
{
    switch (s_state)
    {
    case ST_MENU:           State_Menu(td_held, lx, ly); break;
    case ST_DEPOSIT_KEYPAD: State_DepositKeypad(td_held, lx, ly); break;
    case ST_DEPOSIT_DONE:   State_DoneBack(td_held, lx, ly); break;
    case ST_PICKUP_KEYPAD:  State_PickupKeypad(td_held, lx, ly); break;
    case ST_PICKUP_DONE:    State_DoneBack(td_held, lx, ly); break;
    case ST_ADMIN:          State_Admin(td_held, lx, ly); break;
    }
}

/******************************************************
**  voice events
**
**  The pickup keypad owns the same digit buffer as the touch pad, so its
**  events are injected straight into the keypad; everywhere else they go
**  through the Voice_Handle* helpers.  Nothing consumes a new digit while a
**  code waits for the "对 / 错" answer (that is s_voice_pending).
******************************************************/
static void Ui_Voice(const app_evt_t *ev)
{
    switch (ev->a)
    {
    case VS_RAW:
        /* the same idiom as Voice_HandleCode(): the pickup-guide overlay
           keeps its partial-code panel, everywhere else (including the chat
           panel) the new line replaces the menu hint */
        if (s_state == ST_MENU && (!s_overlay || s_chat_mode))
        {
            Menu_DrawVoxHint(ev->text);
            Ui_HintStart(ev->b ? VOX_HINT_LONG_MS : VOX_HINT_MS);
        }
        break;

    case VS_CHAT_WAKE:
        Voice_HandleChatWake();
        break;

    case VS_CHAT:
        if (s_chat_mode) Chat_HandleKeyword(ev->b);
        break;

    case VS_DIGIT:
    case VS_UNDO:
    case VS_PENDING:
    case VS_RETRY:
    case VS_CODE:
    case VS_CMD:
        if (s_state == ST_PICKUP_KEYPAD)
        {
            switch (ev->a)
            {
            case VS_DIGIT:
                Keypad_InjectDigit((char)(0x30 + ev->c));
                printf("[VOICE][KPAD] digit val=%d\r\n", (int)ev->c);
                break;
            case VS_UNDO:
            {
                char tmp[7];
                uint8_t n = Keypad_GetCode(tmp);
                uint8_t k;
                Keypad_ClearAll();
                for (k = 0; k + 1 < n; k++)
                    Keypad_InjectDigit(tmp[k]);
                s_kpad_confirm = 0;
                printf("[VOICE][KPAD] undo\r\n");
                break;
            }
            case VS_PENDING:
            {
                uint8_t k;
                Keypad_ClearAll();
                for (k = 0; k < 6; k++)
                    Keypad_InjectDigit(ev->text[k]);
                printf("[VOICE][KPAD] pending %s\r\n", ev->text);
                Kpad_AnnounceConfirm();
                break;
            }
            case VS_RETRY:
                Keypad_ClearAll();
                s_kpad_confirm = 0;
                SYN_FrameInfo(0, (uint8_t *)"[v8][t5][m0]\xC7\xEB\xD6\xD8\xCB\xB5");
                StatusBar_Set(SRC_VOICE, 0, "\xC7\xEB\xD6\xD8\xD0\xC2\xCA\xE4\xC8\xEB");
                printf("[VOICE][KPAD] retry\r\n");
                break;
            case VS_CODE:
                printf("[VOICE][KPAD] confirm %s\r\n", ev->text);
                Kpad_DoPickup();
                break;
            default: /* VS_CMD */
                if (ev->b == LD_CMD_PASSWORD)
                {
                    SYN_FrameInfo(0, (uint8_t *)"[v8][t5][m0]\xD4\xDA\xB5\xC4\xC7\xEB\xCB\xB5");
                    StatusBar_Set(SRC_VOICE, 0, "\xC7\xEB\xCB\xB5\xC8\xA1\xBC\xFE\xC2\xEB");
                    printf("[VOICE][KPAD] wake\r\n");
                }
                else if (ev->b == LD_CMD_NO_PASSWD)
                {
                    /* keep the keypad screen & the entered digits: just ask
                       the user to wake the recognition again */
                    printf("[VOICE][KPAD] timeout (keep screen)\r\n");
                    StatusBar_Set(SRC_VOICE, 0, "\xC7\xEB\xCB\xB5\xD0\xA1\xD3\xA3\xBC\xCC\xD0\xF8");
                }
                else
                {
                    printf("[CMD] %d", (int)ev->b);
                }
                break;
            }
        }
        else
        {
            char code[7];
            memcpy(code, ev->text, 7);   /* text is APP_EV_TEXT_MAX bytes */
            code[6] = 0;
            switch (ev->a)
            {
            case VS_DIGIT:
                Voice_HandleDigit(ev->b, ev->c);
                break;
            case VS_UNDO:
                Voice_HandleUndo();
                break;
            case VS_PENDING:
                s_voice_pending = 1;
                Ui_HintCancel();
                printf("[VOICE] pending %s\r\n", code);
                Voice_HandlePending(code);
                break;
            case VS_RETRY:
                s_voice_pending = 0;
                Voice_HandleRetry();
                break;
            case VS_CODE:
                s_voice_pending = 0;
                printf("[VOICE] code %s\r\n", code);
                Voice_HandleCode(code);
                break;
            default: /* VS_CMD */
                if (ev->b == LD_CMD_PASSWORD)
                {
                    if (s_chat_mode)
                    {
                        s_chat_mode = 0;
                        s_chat_busy = 0;
                        printf("[CHAT] exit wake\r\n");
                    }
                    Voice_HandleWake();
                }
                else if (ev->b == LD_CMD_NO_PASSWD)
                {
                    if (s_chat_mode)
                    {
                        s_chat_mode = 0;
                        s_chat_busy = 0;
                        printf("[CHAT] exit timeout\r\n");
                    }
                    printf("[VOICE] timeout\r\n");
                    s_voice_pending = 0;
                    s_vplen    = 0;
                    s_vpart[0] = 0;
                    StatusBar_Set(SRC_VOICE, 0, "\xD3\xEF\xD2\xF4\xB3\xAC\xCA\xB1\xC7\xEB\xD6\xD8\xCB\xB5");
                    if (s_state == ST_MENU)
                        Menu_DrawVoxHint("\xB3\xAC\xCA\xB1 \xD6\xD8\xCB\xB5\xD0\xA1\xD3\xA3");
                }
                else
                {
                    printf("[CMD] %d", (int)ev->b);
                }
                break;
            }
        }
        break;

    default:
        break;
    }
}

/******************************************************
**  a door change: move the UI-side shadow, refresh the band
******************************************************/
static void Ui_DoorState(const app_evt_t *ev)
{
    Door_ShadowSet(ev->a, ev->b, ev->c);
    if (s_state == ST_DEPOSIT_DONE || s_state == ST_PICKUP_DONE)
        DoorStatus_Update();
}

/******************************************************
**  the flow is over: finish it exactly like the superloop did
**
**  Order matters and is preserved: release the code (pickup), tell the
**  server, speak, then the status bar.  Only then does the DONE screen
**  step aside - and only if it is still showing this box.
******************************************************/
static void Ui_DoorDone(const app_evt_t *ev)
{
    uint8_t box  = ev->a;
    uint8_t flow = ev->b;
    uint8_t src  = ev->c;
    char    line[CLOUD_PEND_LEN];

    Door_ShadowSet(box, DOOR_DONE, flow);

    if (ev->x)
    {
        /* 30s timeout: the box was left ajar, the item is taken */
        CodeBox_Clear(box);
        sprintf(line, "PICKED %d", (int)box);
        Cloud_Queue(line);
        SYN_FrameInfo(0, (uint8_t *)((flow == 1) ? "[v8][t5][m0]\xCD\xB6\xBC\xFE\xCD\xEA\xB3\xC9"
                                                : "[v8][t5][m0]\xC8\xA1\xBC\xFE\xCD\xEA\xB3\xC9"));
        StatusBar_Set(src, box, (flow == 1) ? "\xCD\xB6\xBC\xFE\xCD\xEA\xB3\xC9"
                                            : "\xB3\xAC\xCA\xB1\xD2\xD1\xCD\xEA\xB3\xC9");
        printf("[DOOR] box %d timeout, flow %d complete\r\n", (int)box, (int)flow);
    }
    else if (flow == 1)
    {
        sprintf(line, "DEPOSITED %d", (int)box);
        Cloud_Queue(line);
        SYN_FrameInfo(0, (uint8_t *)"[v8][t5][m0]\xCD\xB6\xBC\xFE\xCD\xEA\xB3\xC9");
        StatusBar_Set(src, box, "\xCD\xB6\xBC\xFE\xCD\xEA\xB3\xC9");
        printf("[DOOR] box %d closed, flow %d complete\r\n", (int)box, (int)flow);
    }
    else if (flow == 2)
    {
        CodeBox_Clear(box);
        sprintf(line, "PICKED %d", (int)box);
        Cloud_Queue(line);
        SYN_FrameInfo(0, (uint8_t *)"[v8][t5][m0]\xC8\xA1\xBC\xFE\xCD\xEA\xB3\xC9");
        StatusBar_Set(src, box, "\xC8\xA1\xBC\xFE\xCD\xEA\xB3\xC9");
        printf("[DOOR] box %d closed, flow %d complete\r\n", (int)box, (int)flow);
    }
    else
    {
        /* remote open: no code to release, just confirm the door closed */
        SYN_FrameInfo(0, (uint8_t *)"[v8][t5][m0]\xC3\xC5\xD2\xD1\xB9\xD8");
        StatusBar_Set(src, box, "\xD2\xD1\xB9\xD8\xC3\xC5");
        printf("[DOOR] box %d closed, flow %d complete\r\n", (int)box, (int)flow);
    }

    if (s_res_box == box &&
        (s_state == ST_PICKUP_DONE || s_state == ST_DEPOSIT_DONE))
    {
        s_state = ST_MENU;
        Menu_Draw();
    }
}

/******************************************************
**  the UI task
******************************************************/
void Ui_Task(void *arg)
{
    uint32_t next_status = 0;
    uint32_t next_band   = 0;
    app_evt_t ev;

    (void)arg;

    for (;;)
    {
        uint8_t   got = (xQueueReceive(q_ui, &ev, pdMS_TO_TICKS(UI_TICK_MS)) == pdTRUE);
        uint32_t  now;

        /* the whole UI path draws, and the clock task draws the clock card
           from the other side; lock order everywhere is lcd -> log */
        Lcd_Lock();

        if (got)
        {
            switch (ev.kind)
            {
            case EV_TOUCH:       Ui_Touch(ev.a, ev.x, ev.y); break;
            case EV_VOICE:       Ui_Voice(&ev); break;
            case EV_CLOUD_LINE:  Cloud_HandleLine(ev.text); break;
            case EV_DOOR_STATE:  Ui_DoorState(&ev); break;
            case EV_DOOR_DONE:   Ui_DoorDone(&ev); break;
            default: break;
            }
        }

        now = App_Ms();

        /* 1) chat lease: 25s of silence ends it */
        if (s_chat_mode && (uint32_t)(now - s_chat_tick) > CHAT_IDLE_MS)
        {
            s_chat_mode  = 0;
            s_chat_busy  = 0;
            StatusBar_Set(SRC_VOICE, 0, "\xB6\xD4\xBB\xB0\xD2\xD1\xB3\xAC\xCA\xB1");
            if (s_state == ST_MENU) Menu_DrawVoxHint(0);
            printf("[CHAT] idle timeout\r\n");
        }

        /* 2) the voice hint line goes back to standby */
        if (s_vox_active && (int32_t)(now - s_vox_until) >= 0)
        {
            s_vox_active = 0;
            if (s_state == ST_MENU && !s_overlay && !s_voice_pending)
                Menu_DrawVoxHint(0);
        }

        /* 3) one STATUS uplink every 2s, if the link is idle */
        if ((int32_t)(now - next_status) >= 0)
        {
            next_status = now + CLOUD_STATUS_MS;
            Cloud_StatusTick();
        }

        /* 4) the door band on the DONE screens (it also follows the door
              contacts, which are not events) */
        if ((int32_t)(now - next_band) >= 0)
        {
            next_band = now + DOOR_FB_POLL_MS;
            if (s_state == ST_DEPOSIT_DONE || s_state == ST_PICKUP_DONE)
                DoorStatus_Update();
        }

        /* 5) publish the lamps the other tasks read */
        g_ui_screen = (uint8_t)((s_state == ST_MENU && !s_overlay) ? 1 : 0);
        g_chat_busy = s_chat_busy;

        Lcd_Unlock();
    }
}
