/******************************************************
**  app_ui.c - title bar, cards, menu, overlays, DONE screens
**             and the status bar.
**
**  Split out of the monolithic main.c; bodies are unchanged.
******************************************************/
#include "app_ui.h"
#include "app_ctx.h"
#include "app_clock.h"
#include "app_door.h"     /* DoorStatus_Draw on the DONE screens */
#include "codebox.h"
#include "esp8266.h"
#include "lcd.h"
#include <stdio.h>
#include <string.h>

/* ---- private state (single-module owners stayed static) ---- */
static char    s_last_msg[40] = "\xCF\xB5\xCD\xB3\xBE\xCD\xD0\xF7";
static uint8_t s_link_shown = 0xFF;   /* 0xFF = repaint needed */

static void StatusBar_Draw(void)
{
    LCD_Fill(0, STATUS_BAR_Y, LCD_W - 1, LCD_H - 1, COLOR_PANEL);
    LCD_Fill(0, STATUS_BAR_Y, LCD_W - 1, STATUS_BAR_Y + 1, COLOR_ACCENT);
    LCD_ShowStringCN(8, STATUS_BAR_Y + 5, s_last_msg, COLOR_TEXT, COLOR_PANEL, 1);
}

void StatusBar_Set(uint8_t src, uint8_t box_id, const char *msg)
{
    char buf[48];
    const char *tag = (src == SRC_VOICE) ? "\xD3\xEF\xD2\xF4" : "\xB4\xA5\xC3\xFE";
    if (box_id)
        snprintf(buf, sizeof(buf), "[%s] #%d %s", tag, box_id, msg);
    else
        snprintf(buf, sizeof(buf), "[%s] %s", tag, msg);
    strncpy(s_last_msg, buf, sizeof(s_last_msg) - 1);
    s_last_msg[sizeof(s_last_msg) - 1] = 0;
    StatusBar_Draw();
}

static void TitleBar_Draw(void)
{
    LCD_FillGradV(0, 0, LCD_W - 1, 33, UI_TITLE_TOP, UI_TITLE_BOT);
    LCD_Fill(0, 34, LCD_W - 1, 35, COLOR_ACCENT);
    LCD_ShowStringCN(14, 1, "\xD6\xC7\xC4\xDC\xBF\xEC\xB5\xDD\xB9\xF1", COLOR_WHITE, UI_TITLE_MID, 2);
    s_link_shown = 0xFF;
}

void TitleBar_DrawLink(void)
{
    uint8_t up = (uint8_t)((ESP8266_IsReady() || ESP8266_FetchBusy()) ? 1 : 0);
    if (s_link_shown == up) return;
    s_link_shown = up;
    LCD_FillRound(340, 4, 470, 30, 13, COLOR_BG);
    LCD_ShowStringCN(381, 9, up ? "\xD2\xD1\xC1\xAC\xBD\xD3" : "\xCE\xB4\xC1\xAC\xBD\xD3",
                     up ? COLOR_GREEN : COLOR_GRAY, COLOR_BG, 1);
}

void PickBtn_Draw(uint8_t pressed)
{
    uint16_t bg = pressed ? 0x1C29 : 0x2E6E;
    LCD_FillRound(PICK_X1, PICK_Y1, PICK_X2, PICK_Y2, 18, bg);
    LCD_DrawRoundRect(PICK_X1, PICK_Y1, PICK_X2, PICK_Y2, 18,
                      pressed ? COLOR_WHITE : 0x5EB1);
    LCD_ShowStringCN(208, 210, "\xC8\xA1\xBC\xFE", COLOR_WHITE, bg, 2);
    LCD_ShowStringCN(176, 248, "\xC7\xEB\xCA\xE4\xC8\xEB\xC1\xF9\xCE\xBB\xC8\xA1\xBC\xFE\xC2\xEB", 0xCFDB, bg, 1);
}

uint8_t PickBtn_Scan(uint8_t touch_down, uint16_t x, uint16_t y)
{
    uint8_t click = 0;
    if (touch_down && x >= PICK_X1 && x <= PICK_X2 &&
                      y >= PICK_Y1 && y <= PICK_Y2)
    {
        if (!s_pick_pressed) { s_pick_pressed = 1; PickBtn_Draw(1); }
    }
    else
    {
        if (s_pick_pressed)
        {
            if (!touch_down) click = 1;   /* lifted on the button = click */
            s_pick_pressed = 0;
            PickBtn_Draw(0);
        }
    }
    return click;
}

void Menu_DrawVoxHint(const char *raw)
{
    char buf[48];
    LCD_Fill(0, 274, LCD_W - 1, 293, COLOR_BG);
    if (raw && raw[0])
    {
        strcpy(buf, "\xCC\xFD\xB5\xBD:");
        strncat(buf, raw, sizeof(buf) - strlen(buf) - 1);
        LCD_ShowStringCN(8, 277, buf, COLOR_YELLOW, COLOR_BG, 1);
    }
    else if (!s_voice_seen && loop_tick > 1000)  /* 5s with no LD3320 data */
    {
        LCD_ShowStringCN(140, 277, "\xD3\xEF\xD2\xF4\xC4\xA3\xBF\xE9\xCE\xDE\xCF\xEC\xD3\xA6 \xC7\xEB\xBC\xEC\xB2\xE9\xBD\xD3\xCF\xDF", COLOR_RED, COLOR_BG, 1);
    }
    else
    {
        LCD_ShowStringCN(156, 277, "\xC8\xA1\xBC\xFE\xCB\xB5\xD0\xA1\xD3\xA3 \xB6\xD4\xBB\xB0\xCB\xB5\xD0\xA1\xD6\xC7", COLOR_DIM, COLOR_BG, 1);
    }
}

void Menu_DrawChatPanel(void)
{
    s_overlay = 1;
    static const char *kw[12] = {
        "\xC4\xE3\xBA\xC3","\xBD\xB2\xB8\xF6\xD0\xA6\xBB\xB0","\xD4\xD9\xBC\xFB","\xD0\xBB\xD0\xBB",
        "\xBF\xAA\xC6\xF4","\xB9\xD8\xB1\xD5","\xC4\xE3\xCA\xC7\xCB\xAD","\xBD\xF1\xCC\xEC\xCC\xEC\xC6\xF8",
        "\xCF\xD6\xD4\xDA\xBC\xB8\xB5\xE3","\xC8\xA1\xBC\xFE","",""
    };
    uint8_t i;
    LCD_Clear(0x18C3);
    LCD_ShowStringCN(170, 8, "\xC7\xEB\xCB\xB5\xB9\xD8\xBC\xFC\xB4\xCA", COLOR_YELLOW, 0x18C3, 2);
    for (i = 0; i < 10; i++)
    {
        uint16_t col = i % 4;
        uint16_t row = i / 4;
        uint16_t x = 20 + col * 115;
        uint16_t y = 50 + row * 55;
        LCD_Fill(x, y, x + 105, y + 45, 0x2E59);
        LCD_ShowStringCN(x + 8, y + 12, kw[i], COLOR_WHITE, 0x2E59, 2);
    }
    LCD_ShowStringCN(120, 210, "\xCB\xB5\xD0\xA1\xD6\xC7\xBB\xBD\xD0\xD1 \xD4\xD9\xCB\xB5\xB9\xD8\xBC\xFC\xB4\xCA", COLOR_YELLOW, 0x18C3, 2);

    s_btn_back.x = 180; s_btn_back.y = 235;
    s_btn_back.w = 120; s_btn_back.h = 45;
    s_btn_back.label = "\xB7\xB5\xBB\xD8";
    s_btn_back.bg_color = COLOR_BLUE;
    s_btn_back.press_color = COLOR_CYAN;
    s_btn_back.text_color = COLOR_WHITE;
    s_btn_back.border_color = COLOR_WHITE;
    s_btn_back.state = 0;
    Button_Draw(&s_btn_back);
    StatusBar_Set(SRC_VOICE, 0, "\xD3\xEF\xD2\xF4\xD2\xD1\xCC\xFD\xB5\xBD");
}

void Guide_ShowPartial(void)
{
    char shown[8];
    char buf[12];
    uint8_t i;
    for (i = 0; i < 6; i++)
        shown[i] = (i < s_vplen) ? s_vpart[i] : '_';
    shown[6] = 0;
    sprintf(buf, "\xD2\xD1\xCA\xE4:%s", shown);
    LCD_Fill(0, 204, LCD_W - 1, 224, 0x18C3);
    LCD_ShowStringCN(182, 206, buf, COLOR_GREEN, 0x18C3, 2);
}

void Menu_DrawPickupGuide(void)
{
    static const char *labels[12] = {
        "0","1","2","3","4","5","6","7","8","9","\xC9\xBE\xB3\xFD","\xD6\xD8\xCA\xE4"
    };
    uint8_t i, row, col;
    s_overlay = 1;
    LCD_Clear(0x18C3);
    LCD_ShowStringCN(200, 8, "\xC7\xEB\xCA\xE4\xC8\xEB\xC8\xA1\xBC\xFE\xC2\xEB", COLOR_YELLOW, 0x18C3, 2);
    for (i = 0; i < 12; i++)
    {
        row = (uint8_t)(i / 3);
        col = (uint8_t)(i % 3);
        s_pg_btns[i].x = (uint16_t)(34 + col * 142);
        s_pg_btns[i].y = (uint16_t)(30 + row * 44);
        s_pg_btns[i].w = 128;
        s_pg_btns[i].h = 40;
        s_pg_btns[i].label = labels[i];
        if (i == 10)
        {
            s_pg_btns[i].bg_color    = COLOR_YELLOW;
            s_pg_btns[i].press_color = COLOR_ORANGE;
            s_pg_btns[i].text_color  = COLOR_BLACK;
        }
        else if (i == 11)
        {
            s_pg_btns[i].bg_color    = 0x8000;
            s_pg_btns[i].press_color = COLOR_RED;
            s_pg_btns[i].text_color  = COLOR_WHITE;
        }
        else
        {
            s_pg_btns[i].bg_color    = COLOR_BLUE;
            s_pg_btns[i].press_color = COLOR_CYAN;
            s_pg_btns[i].text_color  = COLOR_WHITE;
        }
        s_pg_btns[i].border_color = COLOR_WHITE;
        s_pg_btns[i].state = 0;
        Button_Draw(&s_pg_btns[i]);
    }
    Guide_ShowPartial();
    LCD_ShowStringCN(20, 228, "0\xB6\xB4" "1\xE7\xDB" "2\xC1\xBD 7\xB9\xD5" "9\xB9\xB4", COLOR_CYAN, 0x18C3, 1);
    LCD_ShowStringCN(150, 228, "\xCB\xB5\xCA\xFD\xD7\xD6\xBB\xF2\xB4\xA5\xC3\xFE\xCA\xE4\xC8\xEB", COLOR_YELLOW, 0x18C3, 1);

    s_btn_back.x = 180; s_btn_back.y = 246;
    s_btn_back.w = 120; s_btn_back.h = 40;
    s_btn_back.label = "\xB7\xB5\xBB\xD8";
    s_btn_back.bg_color = COLOR_BLUE;
    s_btn_back.press_color = COLOR_CYAN;
    s_btn_back.text_color = COLOR_WHITE;
    s_btn_back.border_color = COLOR_WHITE;
    s_btn_back.state = 0;
    Button_Draw(&s_btn_back);
    StatusBar_Set(SRC_VOICE, 0, "\xC7\xEB\xCB\xB5\xBB\xF2\xB4\xA5\xC3\xFE\xCA\xE4\xC8\xEB\xC8\xA1\xBC\xFE\xC2\xEB");
}

void Menu_DrawAdmin(void)
{
    uint8_t i;
    CodeEntry e;
    char buf[32];

    s_overlay = 1;
    LCD_Clear(0x0000);

    /* header */
    LCD_Fill(0, 0, LCD_W - 1, 28, 0x3186);
    LCD_ShowStringCN(208, 6, "\xC8\xA1\xBC\xFE\xBC\xC7\xC2\xBC", COLOR_WHITE, 0x3186, 2);

    /* 4 boxes */
    for (i = 0; i < CODEBOX_COUNT; i++)
    {
        uint16_t y = 36 + i * 52;
        LCD_Fill(10, y, LCD_W - 11, y + 48, (i % 2) ? 0x0B6D : 0x0D10);

        if (CodeBox_GetEntry(i + 1, &e))
        {
            /* occupied: show box num + phone + code */
            sprintf(buf, "Box %d: %s", (int)(i + 1), e.code);
            LCD_ShowStringCN(20, y + 6, buf, COLOR_GREEN, (i % 2) ? 0x0B6D : 0x0D10, 2);
            sprintf(buf, "Tel: %s", e.phone);
            LCD_ShowStringCN(20, y + 28, buf, COLOR_YELLOW, (i % 2) ? 0x0B6D : 0x0D10, 2);
        }
        else
        {
            sprintf(buf, "Box %d: Kong", (int)(i + 1));
            LCD_ShowStringCN(20, y + 16, buf, COLOR_GRAY, (i % 2) ? 0x0B6D : 0x0D10, 2);
        }
    }

    /* back button */
    s_btn_admin_back.x = 180; s_btn_admin_back.y = 244;
    s_btn_admin_back.w = 120; s_btn_admin_back.h = 45;
    s_btn_admin_back.label = "\xB7\xB5\xBB\xD8";
    s_btn_admin_back.bg_color = COLOR_BLUE;
    s_btn_admin_back.press_color = COLOR_CYAN;
    s_btn_admin_back.text_color = COLOR_WHITE;
    s_btn_admin_back.border_color = COLOR_WHITE;
    s_btn_admin_back.state = 0;
    Button_Draw(&s_btn_admin_back);
}

void Menu_Draw(void)
{
    s_overlay = 0;
    LCD_Clear(COLOR_BG);

    TitleBar_Draw();
    TitleBar_DrawLink();
    Clock_DrawCard();
    Wx_DrawCard();

    s_pick_pressed = 0;
    PickBtn_Draw(0);

    Menu_DrawVoxHint(0);
    StatusBar_Draw();
}

void DepositDone_Draw(void)
{
    char buf[32];
    LCD_Clear(COLOR_BLACK);

    LCD_Fill(0, 0, LCD_W - 1, 28, 0x3186);
    LCD_ShowStringCN(224, 6, "\xCD\xB6\xBC\xFE", COLOR_WHITE, 0x3186, 2);

    if (s_res_ok)
    {
        sprintf(buf, "#%d", s_res_box);
        LCD_ShowStringCN(202, 48, buf, COLOR_GREEN, COLOR_BLACK, 2);
        sprintf(buf, "%s", s_res_code);
        LCD_ShowStringCN(174, 90, buf, COLOR_YELLOW, COLOR_BLACK, 2);
    }
    else
    {
    LCD_ShowStringCN(216, 70, "\xD2\xD1\xBF\xAA\xC3\xC5", COLOR_RED, COLOR_BLACK, 2);
        LCD_ShowStringCN(216, 110, s_res_msg, COLOR_YELLOW, COLOR_BLACK, 2);
    }

    s_btn_back.x = 180; s_btn_back.y = 220;
    s_btn_back.w = 120; s_btn_back.h = 50;
    s_btn_back.label = "\xB7\xB5\xBB\xD8";
    s_btn_back.bg_color = COLOR_BLUE;
    s_btn_back.press_color = COLOR_CYAN;
    s_btn_back.text_color = COLOR_WHITE;
    s_btn_back.border_color = COLOR_WHITE;
    s_btn_back.state = 0;
    Button_Draw(&s_btn_back);

    DoorStatus_Draw(132);
    StatusBar_Draw();
}

void PickupDone_Draw(void)
{
    char buf[32];
    LCD_Clear(COLOR_BLACK);

    LCD_Fill(0, 0, LCD_W - 1, 28, 0x3186);
    LCD_ShowStringCN(224, 6, "\xC8\xA1\xBC\xFE", COLOR_WHITE, 0x3186, 2);

    if (s_res_ok)
    {
        sprintf(buf, "%d\xBA\xC5\xCF\xE4\xD2\xD1\xB4\xF2\xBF\xAA", s_res_box);
        LCD_ShowStringCN(180, 48, buf, COLOR_GREEN, COLOR_BLACK, 2);

        s_btn_continue.x = 60;  s_btn_continue.y = 210;
        s_btn_continue.w = 160; s_btn_continue.h = 50;
        s_btn_continue.label = "\xD4\xD9\xC8\xA1\xD2\xBB\xBC\xFE";
        s_btn_continue.bg_color = COLOR_GREEN;
        s_btn_continue.press_color = COLOR_CYAN;
        s_btn_continue.text_color = COLOR_WHITE;
        s_btn_continue.border_color = COLOR_WHITE;
        s_btn_continue.state = 0;
        Button_Draw(&s_btn_continue);

        s_btn_back.x = 260; s_btn_back.y = 210;
        s_btn_back.w = 160; s_btn_back.h = 50;
        s_btn_back.label = "\xB7\xB5\xBB\xD8";
        s_btn_back.bg_color = COLOR_BLUE;
        s_btn_back.press_color = COLOR_CYAN;
        s_btn_back.text_color = COLOR_WHITE;
        s_btn_back.border_color = COLOR_WHITE;
        s_btn_back.state = 0;
        Button_Draw(&s_btn_back);
    }
    else
    {
        LCD_ShowStringCN(200, 60, "\xC8\xA1\xBC\xFE\xC2\xEB\xCE\xDE\xD0\xA7", COLOR_RED, COLOR_BLACK, 2);
        LCD_ShowStringCN(200, 160, s_res_msg, COLOR_YELLOW, COLOR_BLACK, 2);

        s_btn_back.x = 180; s_btn_back.y = 210;
        s_btn_back.w = 120; s_btn_back.h = 50;
        s_btn_back.label = "\xB7\xB5\xBB\xD8";
        s_btn_back.bg_color = COLOR_BLUE;
        s_btn_back.press_color = COLOR_CYAN;
        s_btn_back.text_color = COLOR_WHITE;
        s_btn_back.border_color = COLOR_WHITE;
        s_btn_back.state = 0;
        Button_Draw(&s_btn_back);
    }

    DoorStatus_Draw(92);
    StatusBar_Draw();
}
