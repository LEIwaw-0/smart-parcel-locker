/******************************************************
**  main.c - boot, peripheral bring-up and the ~5ms main loop.
**
**  This file is all that is left of the original 2266-line monolith.
**  The application logic now lives in the app_* modules beside it,
**  grouped by responsibility:
**
**      app_config.h  project-wide constants and layout macros
**      app_ctx.c/h   shared mutable state (was file-scope statics)
**      app_ui.c/h    title bar, cards, menu, overlays, status bar
**      app_clock.c/h wall clock + weather fetch over the ESP8266 link
**      app_door.c/h  relay drive and door-open feedback tracking
**      app_voice.c/h LD3320 voice front end + AI chat mode
**      app_cloud.c/h TCP link to the locker server (HELLO/STATUS/OPEN...)
**      app_flow.c/h  the deposit / pickup / admin state handlers
**
**  main() keeps only what is genuinely loop-level: touch de-bounce,
**  the 200ms peripheral polls, the voice line parser and the ST_*
**  dispatch.
**
**  Copyright notice and hardware notes preserved from the original.
******************************************************/
#include "app_config.h"
#include "app_ctx.h"
#include "app_cloud.h"
#include "app_clock.h"
#include "app_door.h"
#include "app_flow.h"
#include "app_ui.h"
#include "app_voice.h"
#include "codebox.h"
#include "delay.h"
#include "esp8266.h"
#include "keypad.h"
#include "lcd.h"
#include "ld3320.h"
#include "lock.h"
#include "pcf8574.h"
#include "syn6288.h"
#include "touch.h"
#include "usart.h"
#include <stdio.h>
#include <string.h>

/* ---- private state: voice-link activity hint countdown ---- */
static uint16_t s_vox_flash = 0;

int main(void)
{
    uint8_t  i;
    uint16_t rx = 0, ry = 0, lx = 0, ly = 0;
    uint8_t  td;

    /* touch debounce */
    uint8_t  td_held   = 0;
    uint8_t  press_cnt = 0;
    uint8_t  rel_cnt   = 0;

    /* heartbeat diagnostic */
    uint16_t hb_div    = 0;
    uint8_t  exp_mask, rb_mask;
    uint8_t  closed_mask;

    /* PCF8574 feedback poll */
    uint16_t fb_div     = 0;
    uint8_t  prev_fbnib = 0xFF;
    uint8_t  fb_first   = 1;
    uint8_t  pcf_addr;

    /* ---- boot serial + reset cause ---- */
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);  /* 2 preemption, 2 sub */
    Usart1_Init(115200);
    delay_ms(100);
    printf("\r\n==========================================\r\n");
    printf("    Smart Locker (voice v2)\r\n");
    printf("==========================================\r\n");

    {
        uint32_t csr = RCC->CSR;
        printf("Reset CSR=0x%08lX:", csr);
        if (csr & RCC_CSR_PORRSTF)  printf(" POR");
        if (csr & RCC_CSR_BORRSTF)  printf(" BOR");
        if (csr & RCC_CSR_PADRSTF)  printf(" PAD");
        if (csr & RCC_CSR_SFTRSTF)  printf(" SFT");
        if (csr & RCC_CSR_WDGRSTF)  printf(" WDG");
        if (csr & RCC_CSR_WWDGRSTF) printf(" WWDG");
        if (csr & RCC_CSR_LPWRRSTF) printf(" LPWR");
        printf("\r\n");
        RCC->CSR |= RCC_CSR_RMVF;
    }

    /* ---- PCF8574 scan ---- */
    printf("BOOT: init I2C1 (PB6/PB7), scan PCF8574...\r\n");
    pcf_addr = PCF8574_Init();
    {
        uint8_t alist[16];
        uint8_t an = PCF8574_ScanBus(alist, 16);
        uint8_t k;
        if (an == 0)
            printf("SCAN: no I2C device at 0x01..0x7E\r\n");
        else
        {
            printf("SCAN: %d device(s):", an);
            for (k = 0; k < an; k++) printf(" 0x%02X", alist[k]);
            printf("\r\n");
        }
    }
    if (pcf_addr == 0)
        printf("BOOT: !! no PCF8574 detected - P4-P7 feedback unavailable\r\n");
    else
        printf("BOOT: PCF8574 at 0x%02X (P4-P7 = feedback input)\r\n", pcf_addr);

    Lock_Init();

    /* ---- 上电安全: 只强制关锁, 绝不做开锁自检 ----
       原自检会让4路继电器各吸合300ms, 等于每次上电都把4个柜门打开一次,
       存在安全隐患。现在上电只把所有线圈置为断开, 再读一次P4-P7门状态反馈,
       这样继电器粘连仍能在串口看到, 但不会打开任何柜门。 */
    printf("BOOT: forcing all locks OFF (no self-test pulse)\r\n");
    for (i = 1; i <= LOCK_COUNT; i++)
        Lock_Close(i);
    delay_ms(100);
    for (i = 1; i <= LOCK_COUNT; i++)
    {
        uint8_t st = Lock_ReadFeedback(i);
        printf("BOOT: lock %d -> %s\r\n", (int)i,
               (st == 1) ? "OPEN" : (st == 0) ? "closed" : "read-fail");
    }


    /* ---- LCD + touch ---- */
    Touch_Init();
    LCD_Init();
    LCD_Backlight(1);

    /* ---- code table (Flash Sector 11) ---- */
    CodeBox_Init();
    printf("BOOT: code table loaded, %d/%d boxes in use\r\n",
           CodeBox_CountUsed(), BOX_COUNT);

    Menu_Draw();
    printf("BOOT: UI ready (menu)\r\n");

    /* ---- SYN6288 + LD3320 ---- */
    SYN6288_Init();
    LD3320_Init();
    ESP8266_Init();
    printf("BOOT: SYN6288 (PC10) + LD3320 (PA3) + ESP8266 (PB10) initialised\r\n");

    SYN_FrameInfo(0, (uint8_t *)"[v8][t5][m0]\xBB\xB6\xD3\xAD\xCA\xB9\xD3\xC3\xBF\xEC\xB5\xDD\xB9\xF1");
    printf("BOOT: welcome spoken\r\n");
    delay_ms(1500);

    printf("BOOT: tap DEPOSIT or PICKUP, or say 'xiao ying' + 6 digits.\r\n\r\n");

    while (1)
    {
        /* 1) relay auto-off (2s solenoid protection) */
        closed_mask = Lock_Tick();
        (void)closed_mask;

        /* 1b) heartbeat: while any relay is on, log mask every 200ms */
        loop_tick++;
        if (Lock_IsOpen(1) || Lock_IsOpen(2) || Lock_IsOpen(3) || Lock_IsOpen(4))
        {
            if (++hb_div >= 40)
            {
                hb_div = 0;
                exp_mask = Lock_GetActiveMask();
                rb_mask  = Lock_HW_ReadBack();
                if (rb_mask == 0xFF)
                    printf("HB: %lums exp=0x%X readback fail\r\n",
                           loop_tick * 5, exp_mask);
                else
                    printf("HB: %lums exp=0x%X real=0x%X %s\r\n",
                           loop_tick * 5, exp_mask, rb_mask,
                           (rb_mask == exp_mask) ? "ok" : "MISMATCH");
            }
        }
        else
        {
            hb_div = 0;
        }

        /* 2) touch scan + debounce */
        td = Touch_Scan(&lx, &ly, &rx, &ry);
        if (td) { if (press_cnt < 255) press_cnt++; rel_cnt = 0; }
        else    { if (rel_cnt   < 255) rel_cnt++;   press_cnt = 0; }
        if (!td_held && press_cnt >= 2)
        {
            td_held = 1;
        }
        if (td_held && rel_cnt >= 4)
        {
            td_held = 0;
        }

        /* 3) state-machine input dispatch */
        switch (s_state)
        {
        case ST_MENU:           State_Menu(td_held, lx, ly); break;
        case ST_DEPOSIT_KEYPAD: State_DepositKeypad(td_held, lx, ly); break;
        case ST_DEPOSIT_DONE:   State_DoneBack(td_held, lx, ly); break;
        case ST_PICKUP_KEYPAD:  State_PickupKeypad(td_held, lx, ly); break;
        case ST_PICKUP_DONE:    State_DoneBack(td_held, lx, ly); break;
        case ST_ADMIN:           State_Admin(td_held, lx, ly); break;
        }

        /* 4) LD3320 voice poll:
           a) echo EVERY raw line on USART1 so wiring/firmware can be checked
           b) CODE:DDDDDD -> pickup flow
           c) CMD:00 (wake word) -> TTS ack (works with factory firmware too) */
        {
            char raw[24];
            if (LD3320_GetRaw(raw, sizeof(raw)))
            {
                s_voice_seen = 1;
                uint8_t show;
                printf("[LD3320] ");
                if (strncmp(raw, "DIGIT:", 6) == 0)
                    printf("digit pos=%d val=%c", (int)(raw[6]-'0')+1, raw[12]);
                else if (strncmp(raw, "CMD:00", 6) == 0)
                    printf("CMD:00 wake");
                else if (strncmp(raw, "CMD:FE", 6) == 0)
                    printf("CMD:FE timeout");
                else if (strncmp(raw, "CHAT:WAKE", 9) == 0)
                    printf("CHAT:WAKE");
                else if (strncmp(raw, "CHAT:", 5) == 0)
                    printf("CHAT 0x%02X%02X", raw[5], raw[6]);
                else if (strncmp(raw, "PCODE:", 6) == 0)
                    printf("PCODE %s", raw+6);
                else if (strncmp(raw, "CODE:", 5) == 0)
                    printf("CODE %s", raw+5);
                else if (strncmp(raw, "RETRY", 5) == 0)
                    printf("RETRY");
                else if (strncmp(raw, "UNDO", 4) == 0)
                    printf("UNDO");
                else if (strncmp(raw, "HB", 2) == 0)
                    printf("HB");
                else if (strncmp(raw, "LD-DIGIT", 8) == 0)
                    printf("\xC4\xA3\xBF\xE9\xBE\xCD\xD0\xF7");
                else if (strncmp(raw, "LD_OK", 5) == 0)
                    printf("\xD7\xD4\xBC\xEC\xCD\xA8\xB9\xFD %s", raw+5);
                else if (strncmp(raw, "LD_ERR", 6) == 0)
                    printf("\xD7\xD4\xBC\xEC\xCA\xA7\xB0\xDC");
                else
                    printf("%s", raw);
                printf("\r\n");
                /* boot beacons/selftest lines go to serial only; only
                   recognition results are shown on the menu hint line */
                show = (strncmp(raw, "LD3320", 6) != 0 &&
                        strncmp(raw, "LD-DIGIT", 8) != 0 &&
                        strncmp(raw, "LD_OK",  5) != 0 &&
                        strncmp(raw, "LD_ERR", 6) != 0 &&
                        strncmp(raw, "PCODE:", 6) != 0 &&  /* dedicated panel */
                        strncmp(raw, "RETRY",  5) != 0 &&   /* dedicated panel */
                        strncmp(raw, "HB",     2) != 0);    /* heartbeat: serial only */
                if (show && s_state == ST_MENU && !s_overlay)
                {
                    if (strncmp(raw, "DIGIT:", 6) == 0)
                    {
                        /* digit capture progress: "DIGIT:n VAL:x" */
                        char hint[24];
                        uint8_t idx = (uint8_t)(raw[6] - '0');   /* 0..5 */
                        char val = raw[12];                       /* value digit */
                        sprintf(hint, "%d:%c", (int)(idx + 1), val);
                        Menu_DrawVoxHint(hint);
                        s_vox_flash = 1000;  /* keep ~5s during entry */
                    }
                    else
                    {
                        Menu_DrawVoxHint(raw);
                        s_vox_flash = 400;
                    }
                }
            }
            {
                char code[7];
                uint8_t didx, dval;
                if (s_state == ST_PICKUP_KEYPAD)
                {
                    /* On the pickup keypad, voice digits share the same
                       buffer as touch. 6 digits full -> announce & wait
                       for "对" or the 确认 button. */
                    if (LD3320_GetDigit(&didx, &dval))
                    {
                        Keypad_InjectDigit((char)(0x30 + dval));
                        printf("[VOICE][KPAD] digit val=%d\r\n", dval);
                    }
                    else if (LD3320_GetUndo())
                    {
                        char tmp[7];
                        uint8_t n = Keypad_GetCode(tmp);
                        uint8_t k;
                        Keypad_ClearAll();
                        for (k = 0; k + 1 < n; k++)
                            Keypad_InjectDigit(tmp[k]);
                        s_kpad_confirm = 0;
                        printf("[VOICE][KPAD] undo\r\n");
                    }
                    else if (LD3320_GetPendingCode(code))
                    {
                        uint8_t k;
                        Keypad_ClearAll();
                        for (k = 0; k < 6; k++)
                            Keypad_InjectDigit(code[k]);
                        printf("[VOICE][KPAD] pending %s\r\n", code);
                        Kpad_AnnounceConfirm();
                    }
                    else if (LD3320_GetRetry())
                    {
                        Keypad_ClearAll();
                        s_kpad_confirm = 0;
                        SYN_FrameInfo(0, (uint8_t *)"[v8][t5][m0]\xC7\xEB\xD6\xD8\xCB\xB5");
                        StatusBar_Set(SRC_VOICE, 0, "\xC7\xEB\xD6\xD8\xD0\xC2\xCA\xE4\xC8\xEB");
                        printf("[VOICE][KPAD] retry\r\n");
                    }
                    else if (LD3320_GetCode(code))
                    {
                        printf("[VOICE][KPAD] confirm %s\r\n", code);
                        Kpad_DoPickup();
                    }
                    else
                    {
                        int cmd = LD3320_GetCmd();
                        if (cmd == LD_CMD_PASSWORD)
                        {
                            SYN_FrameInfo(0, (uint8_t *)"[v8][t5][m0]\xD4\xDA\xB5\xC4\xC7\xEB\xCB\xB5");
                            StatusBar_Set(SRC_VOICE, 0, "\xC7\xEB\xCB\xB5\xC8\xA1\xBC\xFE\xC2\xEB");
                            printf("[VOICE][KPAD] wake\r\n");
                        }
                        else if (cmd == LD_CMD_NO_PASSWD)
                        {
                            /* Keep the keypad screen & entered digits;
                               just prompt the user to re-wake. */
                            printf("[VOICE][KPAD] timeout (keep screen)\r\n");
                            StatusBar_Set(SRC_VOICE, 0, "\xC7\xEB\xCB\xB5\xD0\xA1\xD3\xA3\xBC\xCC\xD0\xF8");
                        }
                        else if (cmd >= 0)
                            printf("[CMD] %d", cmd);
                    }
                }
                else
                {
                    if (LD3320_GetDigit(&didx, &dval))
                        Voice_HandleDigit(didx, dval);
                    else if (LD3320_GetUndo())
                        Voice_HandleUndo();
                    else if (LD3320_GetPendingCode(code))
                    {
                        s_voice_pending = 1;
                        s_vox_flash    = 0;
                        printf("[VOICE] pending %s\r\n", code);
                        Voice_HandlePending(code);
                    }
                    else if (LD3320_GetRetry())
                    {
                        s_voice_pending = 0;
                        Voice_HandleRetry();
                    }
                    else if (LD3320_GetCode(code))
                    {
                        s_voice_pending = 0;
                        printf("[VOICE] code %s\r\n", code);
                        Voice_HandleCode(code);
                    }
                    else
                    {
                        int cmd = LD3320_GetCmd();
                        if (cmd == LD_CMD_PASSWORD)
                        {
                            if (s_chat_mode)
                            {
                                s_chat_mode = 0;
                                s_chat_busy = 0;
                                printf("[CHAT] exit wake\r\n");
                            }
                            Voice_HandleWake();
                        }
                        else if (cmd == LD_CMD_NO_PASSWD)
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
                        else if (cmd >= 0)
                            printf("[CMD] %d", cmd);
                    }
                }
            }
        }
        /* ---- ESP8266 WiFi + chat mode ---- */
        ESP8266_Tick();
        if (LD3320_GetChatWake())
            Voice_HandleChatWake();
        if (s_chat_mode)
        {
            uint8_t ccode;
            if (LD3320_GetChat(&ccode))
                Chat_HandleKeyword(ccode);
            if ((loop_tick - s_chat_tick) > CHAT_IDLE_TICKS)
            {
                s_chat_mode = 0;
                s_chat_busy = 0;
                StatusBar_Set(SRC_VOICE, 0, "\xB6\xD4\xBB\xB0\xD2\xD1\xB3\xAC\xCA\xB1");
                if (s_state == ST_MENU)
                    Menu_DrawVoxHint(0);
                printf("[CHAT] idle timeout\r\n");
            }
        }

        /* ---- 云端下行: 与对话模式无关, 必须无条件排空接收队列 ---- */
        {
            char cline[200];
            uint8_t guard = 0;
            while (ESP8266_GetResponse(cline, (uint8_t)sizeof(cline)) && guard < 4)
            {
                guard++;
                Cloud_HandleLine(cline);
            }
        }
        Cloud_Tick();

        /* restore default menu hint after the raw-data flash

           (but never while waiting for the dui/cuo answer) */
        if (s_vox_flash)
        {
            if (--s_vox_flash == 0 && s_state == ST_MENU && !s_overlay && !s_voice_pending)
                Menu_DrawVoxHint(0);
        }

        /* 5) PCF8574 P4-P7 feedback poll (200ms) */
        if (++fb_div >= 40)
        {
            uint8_t fraw;
            fb_div = 0;
            if (PCF8574_ReadByte(&fraw) == 0)
            {
                uint8_t nib = (uint8_t)((fraw >> 4) & 0x0F);
                if (fb_first || nib != prev_fbnib)
                {
                    printf("FB P7..P4=%d%d%d%d\r\n",
                           (nib >> 3) & 1, (nib >> 2) & 1,
                           (nib >> 1) & 1, nib & 1);
                    prev_fbnib = nib;
                    fb_first   = 0;
                }
            }

            /* live door tracking on the DONE screens (200ms) */
            Door_Tick();   /* per-box tracking: runs always */
            if (s_state == ST_DEPOSIT_DONE || s_state == ST_PICKUP_DONE)
            {
                DoorStatus_Update();

            }

        }

        /* clock, link badge and the periodic borrowed-link weather fetch */
        Clock_Weather_Tick();

        delay_ms(5);

    }
}
