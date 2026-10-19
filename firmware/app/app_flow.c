/******************************************************
**  app_flow.c - the user-facing flows driven by the state machine.
**
**  Split out of the monolithic main.c; bodies are unchanged.
******************************************************/
#include "app_flow.h"
#include "app_ctx.h"
#include "app_rtos.h"     /* App_Ms */
#include "app_door.h"
#include "app_ui.h"
#include "app_voice.h"
#include "button.h"
#include "codebox.h"
#include "keypad.h"
#include "lcd.h"
#include "syn6288.h"
#include <stdio.h>
#include <string.h>

void State_Admin(uint8_t td_held, uint16_t lx, uint16_t ly)
{
    if (Button_Scan(&s_btn_admin_back, td_held, lx, ly))
    {
        s_overlay = 0;
        s_state = ST_MENU;
        Menu_Draw();
    }
}

void State_Menu(uint8_t td_held, uint16_t lx, uint16_t ly)
{
    uint8_t i;
    static uint32_t s_title_hold = 0;
    static uint16_t s_title_w    = 0;

    /* When chat panel / pickup guide overlay is shown, only the BACK
       button is active (the menu underneath is hidden). */
    if (s_overlay)
    {
        /* pickup guide overlay: digits can be tapped as well as spoken */
        if (!s_chat_mode)
        {
            for (i = 0; i < 10; i++)
            {
                if (Button_Scan(&s_pg_btns[i], td_held, lx, ly))
                {
                    Voice_TouchDigit((uint8_t)i);
                    return;
                }
            }
            if (Button_Scan(&s_pg_btns[10], td_held, lx, ly))
            {
                Voice_HandleUndo();
                return;
            }
            if (Button_Scan(&s_pg_btns[11], td_held, lx, ly))
            {
                Voice_TouchReset();
                return;
            }
        }
        if (Button_Scan(&s_btn_back, td_held, lx, ly))
        {
            s_chat_mode = 0;
            s_chat_busy = 0;
            s_vplen = 0;
            s_vpart[0] = 0;
            Menu_Draw();
            StatusBar_Set(SRC_TOUCH, 0, "\xD2\xD1\xC8\xA1\xCF\xFB");
        }
        return;
    }

    /* ---- hidden entry: hold the title bar for 3 seconds ----
       The touch task re-announces a held finger every 50ms, so the progress
       bar advances in 50ms steps; the start of the hold is remembered as a
       timestamp so the 3s budget does not depend on the event cadence. */
    if (td_held && ly <= 35)
    {
        uint16_t w;
        uint32_t held_ms;
        /* finger slid up from the PICKUP button: drop its latched press so
           releasing on the title bar does not fire a pickup */
        if (s_pick_pressed) { s_pick_pressed = 0; PickBtn_Draw(0); }
        if (s_title_hold == 0) s_title_hold = App_Ms();
        held_ms = App_Ms() - s_title_hold;
        if (held_ms > TITLE_HOLD_MS) held_ms = TITLE_HOLD_MS;

        w = (uint16_t)((held_ms * (LCD_W - 1)) / TITLE_HOLD_MS);
        if (w != s_title_w)
        {
            s_title_w = w;
            LCD_Fill(0, 34, w, 37, COLOR_YELLOW);
            if (w < LCD_W - 1) LCD_Fill((uint16_t)(w + 1), 34, LCD_W - 1, 37, COLOR_BG);
        }
        if (held_ms >= TITLE_HOLD_MS)
        {
            s_title_hold = 0;
            s_title_w    = 0;
            LCD_Fill(0, 34, LCD_W - 1, 35, COLOR_ACCENT);
            LCD_Fill(0, 36, LCD_W - 1, 37, COLOR_BG);
            if (lx < LCD_W / 2)
            {
                s_state = ST_DEPOSIT_KEYPAD;
                Keypad_Begin("\xCD\xB6\xBC\xFE", "\xC7\xEB\xCA\xE4\xC8\xEB\xCA\xD6\xBB\xFA\xBA\xC5");
                StatusBar_Set(SRC_TOUCH, 0, "\xCD\xB6\xBC\xFE:\xC7\xEB\xCA\xE4\xC8\xEB\xCA\xD6\xBB\xFA\xBA\xC5");
            }
            else
            {
                s_state = ST_ADMIN;
                Menu_DrawAdmin();
            }
        }
        return;
    }
    if (s_title_hold)
    {
        s_title_hold = 0;
        s_title_w    = 0;
        LCD_Fill(0, 34, LCD_W - 1, 35, COLOR_ACCENT);
        LCD_Fill(0, 36, LCD_W - 1, 37, COLOR_BG);
    }

    if (PickBtn_Scan(td_held, lx, ly))
    {
        s_state = ST_PICKUP_KEYPAD;
        Keypad_Begin("\xC8\xA1\xBC\xFE", "\xC7\xEB\xCA\xE4\xC8\xEB\xC1\xF9\xCE\xBB\xCA\xFD\xD7\xD6\xC8\xA1\xBC\xFE\xC2\xEB");
        StatusBar_Set(SRC_TOUCH, 0, "\xC8\xA1\xBC\xFE:\xC7\xEB\xCA\xE4\xC8\xEB\xC8\xA1\xBC\xFE\xC2\xEB");
    }
}

void State_DepositKeypad(uint8_t td_held, uint16_t lx, uint16_t ly)
{
    char buf[8];
    KpResult r = Keypad_Scan(td_held, lx, ly, buf);
    if (r == KP_CANCEL)
    {
        s_state = ST_MENU;
        Menu_Draw();
        StatusBar_Set(SRC_TOUCH, 0, "\xC8\xA1\xBC\xFE\xC2\xEB\xD2\xD1\xC8\xA1\xBC\xFE");
    }
    else if (r == KP_DONE)
    {
        uint8_t phone[PHONE_LEN];
        uint8_t i;
        uint8_t out_code[CODE_LEN + 1];
        uint8_t box;

        for (i = 0; i < PHONE_LEN; i++) phone[i] = (uint8_t)buf[i];
        printf("[KEYPAD] Deposit phone=%.6s buf=%s\r\n", phone, buf);

        box = CodeBox_Deposit(phone, out_code);

        s_res_box = box;
        s_res_ok = (box != 0) ? 1 : 0;
        printf("[KEYPAD] Pickup result: box=%d ok=%d\r\n", box, s_res_ok);
        if (box)
        {
            char msg[24];
            memcpy(s_res_code, out_code, CODE_LEN);
            s_res_code[CODE_LEN] = 0;
            DoOpenBox(box, SRC_TOUCH, 1);
            sprintf(msg, "deposit OK #%d code=%s", box, s_res_code);
            StatusBar_Set(SRC_TOUCH, box, msg);
            SYN_FrameInfo(0, (uint8_t *)"[v8][t5][m0]\xC3\xC5\xD2\xD1\xBF\xAA\xC7\xEB\xCD\xB6\xBC\xFE");
            printf("[DEPOSIT] phone %s -> box %d code %s\r\n", buf, box, s_res_code);
        }
        else
        {

            strcpy(s_res_msg, "\xCF\xE4\xD2\xD1\xC2\xFA");
            StatusBar_Set(SRC_TOUCH, 0, "\xCF\xE4\xD2\xD1\xC2\xFA");
            printf("[DEPOSIT] phone %s -> all boxes full\r\n", buf);
        }
        s_state = ST_DEPOSIT_DONE;
        DepositDone_Draw();
    }
}

void Kpad_AnnounceConfirm(void)
{
    char code[7];
    char tts[64];
    uint8_t i;
    Keypad_GetCode(code);
    s_kpad_confirm = 1;
    strcpy(tts, "[v8][t5][m0]");
    strcat(tts, "\xC4\xFA\xB5\xC4\xC8\xA1\xBC\xFE\xC2\xEB\xCA\xC7");
    for (i = 0; i < 6; i++)
        strcat(tts, cn_digit[(uint8_t)(code[i] - 0x30)]);
    strcat(tts, ",\xB6\xD4\xC2\xF0");
    SYN_FrameInfo(0, (uint8_t *)tts);
    StatusBar_Set(SRC_VOICE, 0, "\xC7\xEB\xC8\xB7\xC8\xCF\xC8\xA1\xBC\xFE\xC2\xEB");
    printf("[KPAD] announce %s\r\n", code);
}

void Kpad_DoPickup(void)
{
    char code[7];
    uint8_t codeb[CODE_LEN];
    uint8_t box, i;
    Keypad_GetCode(code);
    for (i = 0; i < CODE_LEN; i++) codeb[i] = (uint8_t)code[i];
    box = CodeBox_PickupByCode(codeb);
    s_res_box = box;
    s_res_ok  = (box != 0) ? 1 : 0;
    s_kpad_confirm = 0;
    if (box)
    {
        char msg[24];
        DoOpenBox(box, SRC_TOUCH, 2);
        sprintf(msg, "pickup OK #%d", box);
        StatusBar_Set(SRC_TOUCH, box, msg);
        SYN_FrameInfo(0, (uint8_t *)"[v8][t5][m0]\xC3\xC5\xD2\xD1\xBF\xAA\xC7\xEB\xC8\xA1\xBC\xFE");
        printf("[KPAD] code %s -> box %d opened\r\n", code, box);
    }
    else
    {
        strcpy(s_res_msg, "\xC8\xA1\xBC\xFE\xC2\xEB\xCE\xDE\xD0\xA7");
        StatusBar_Set(SRC_TOUCH, 0, "\xC8\xA1\xBC\xFE\xC2\xEB\xCE\xDE\xD0\xA7");
        SYN_FrameInfo(0, (uint8_t *)"[v8][t5][m0]\xCA\xB6\xB1\xF0\xCE\xDE\xD0\xA7");
        printf("[KPAD] code %s -> not found\r\n", code);
    }
    s_state = ST_PICKUP_DONE;
    PickupDone_Draw();
}

void State_PickupKeypad(uint8_t td_held, uint16_t lx, uint16_t ly)
{
    char buf[8];
    KpResult r = Keypad_Scan(td_held, lx, ly, buf);
    if (r == KP_CANCEL)
    {
        s_kpad_confirm = 0;
        s_state = ST_MENU;
        Menu_Draw();
        StatusBar_Set(SRC_TOUCH, 0, "\xC8\xA1\xBC\xFE\xC2\xEB\xD2\xD1\xC8\xA1\xCF\xFB");
    }
    else if (r == KP_DONE)   /* 确认 button pressed with 6 digits */
    {
        Kpad_DoPickup();
    }
    else
    {
        /* Auto-announce once the buffer becomes full (touch or voice).
           If the user deletes digits, reset the confirm flag so the
           announcement fires again when it fills back up. */
        if (Keypad_IsFull())
        {
            if (!s_kpad_confirm)
                Kpad_AnnounceConfirm();
        }
        else
        {
            s_kpad_confirm = 0;
        }
    }
}

void State_DoneBack(uint8_t td_held, uint16_t lx, uint16_t ly)
{
    /* On pickup success the screen offers "再取一件" which returns to the
       keypad without disturbing the in-progress door flow; the box is
       released automatically when the door closes or the 30s timer fires. */
    if (s_state == ST_PICKUP_DONE && s_res_ok)
    {
        if (Button_Scan(&s_btn_continue, td_held, lx, ly))
        {
            s_state = ST_PICKUP_KEYPAD;
            Keypad_Begin("\xC8\xA1\xBC\xFE", "\xC7\xEB\xCA\xE4\xC8\xEB\xC1\xF9\xCE\xBB\xCA\xFD\xD7\xD6\xC8\xA1\xBC\xFE\xC2\xEB");
            StatusBar_Set(SRC_TOUCH, 0, "\xC8\xA1\xBC\xFE:\xC7\xEB\xCA\xE4\xC8\xEB\xC8\xA1\xBC\xFE\xC2\xEB");
            return;
        }
    }
    if (Button_Scan(&s_btn_back, td_held, lx, ly))
    {

        s_state = ST_MENU;
        Menu_Draw();
    }
}
