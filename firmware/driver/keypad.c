/******************************************************
**  keypad.c - on-screen 6-digit numeric keypad widget
**
**  Depends on button.c / lcd.c. Buttons are drawn into a
**  static array and scanned with Button_Scan.
******************************************************/
#include "keypad.h"
#include "lcd.h"
#include <string.h>

/* 布局常量(480x320横屏) * 标题(24)+提示(16)+输入格(40)=80; 按键从y=88开始。 * 5行*36高+4*4间距=196; 结束于92+196=288。状态栏在296。 */
#define KP_TITLE_H     24
#define KP_PROMPT_H    16
#define KP_CELL_W      44
#define KP_CELL_H      40
#define KP_CELL_GAP    10
#define KP_CELL_Y      (KP_TITLE_H + KP_PROMPT_H + 4)
/* 6个输入格: 总宽=6*44+5*10=314; 居中在(480-314)/2=83 */
#define KP_CELL_X0     83

#define KP_BTN_W       72
#define KP_BTN_H       36
#define KP_BTN_GAP_X   10
#define KP_BTN_GAP_Y   4
#define KP_BTN_Y0      (KP_CELL_Y + KP_CELL_H + 8)
/* 4列: 4*72+3*10=318; 居中在(480-318)/2=81 */
#define KP_BTN_X0      81

/* 13 buttons: '1'..'9','<'(back),'0','重输','确认'(spans 3 cols) */
#define KP_N_BTNS  13

static Button_t s_btns[KP_N_BTNS];
static char     s_digits[KP_DIGITS + 1];
static uint8_t  s_count = 0;
static char     s_title[24];
static char     s_prompt[32];

/* 绘制6个输入格及当前数字。 */
static void Kp_DrawCells(void)
{
    uint8_t i;
    uint16_t x = KP_CELL_X0;
    for (i = 0; i < KP_DIGITS; i++)
    {
        uint16_t fc, bc;
        if (i < s_count)
        {
            fc = COLOR_WHITE;
            bc = COLOR_BLUE;
        }
        else
        {
            fc = COLOR_GRAY;
            bc = COLOR_BLACK;
        }
        LCD_Fill(x, KP_CELL_Y, x + KP_CELL_W - 1, KP_CELL_Y + KP_CELL_H - 1, bc);
        LCD_DrawRect(x, KP_CELL_Y, x + KP_CELL_W - 1, KP_CELL_Y + KP_CELL_H - 1, COLOR_WHITE);
        if (i < s_count)
        {
            char ch[2] = {s_digits[i], 0};
            LCD_ShowString(x + (KP_CELL_W - 12) / 2,
                           KP_CELL_Y + (KP_CELL_H - 16) / 2,
                           ch, fc, bc, 2);
        }
        else
        {
            /* 下一格的光标 */
            LCD_ShowString(x + (KP_CELL_W - 12) / 2,
                           KP_CELL_Y + (KP_CELL_H - 16) / 2,
                           "_", fc, bc, 2);
        }
        x = (uint16_t)(x + KP_CELL_W + KP_CELL_GAP);
    }
}

/* 5 rows: 1 2 3 | 4 5 6 | 7 8 9 | < 0 重输 | 确认(full width) */
static void Kp_SetupBtns(void)
{
    static const char *labels[KP_N_BTNS] = {
        "1","2","3","4","5","6","7","8","9","<","0","重输","确认"
    };
    uint8_t i, row, col;
    for (i = 0; i < KP_N_BTNS; i++)
    {
        row = (uint8_t)(i / 3);
        col = (uint8_t)(i % 3);
        s_btns[i].x = (uint16_t)(KP_BTN_X0 + col * (KP_BTN_W + KP_BTN_GAP_X));
        s_btns[i].y = (uint16_t)(KP_BTN_Y0 + row * (KP_BTN_H + KP_BTN_GAP_Y));
        s_btns[i].w = KP_BTN_W;
        s_btns[i].h = KP_BTN_H;
        s_btns[i].label = labels[i];
        if (i == 9) /* 退格(<) */
        {
            s_btns[i].bg_color     = COLOR_YELLOW;
            s_btns[i].press_color  = COLOR_ORANGE;
            s_btns[i].text_color   = COLOR_BLACK;
            s_btns[i].border_color = COLOR_WHITE;
        }
        else if (i == 11) /* 重输 */
        {
            s_btns[i].bg_color     = COLOR_ORANGE;
            s_btns[i].press_color  = COLOR_YELLOW;
            s_btns[i].text_color   = COLOR_BLACK;
            s_btns[i].border_color = COLOR_WHITE;
        }
        else if (i == 12) /* 确认 (spans 3 columns) */
        {
            s_btns[i].x            = KP_BTN_X0;
            s_btns[i].w            = (uint16_t)(3 * KP_BTN_W + 2 * KP_BTN_GAP_X);
            s_btns[i].bg_color     = COLOR_GREEN;
            s_btns[i].press_color  = COLOR_CYAN;
            s_btns[i].text_color   = COLOR_BLACK;
            s_btns[i].border_color = COLOR_WHITE;
        }
        else /* 数字 */
        {
            s_btns[i].bg_color     = COLOR_BLUE;
            s_btns[i].press_color  = COLOR_CYAN;
            s_btns[i].text_color   = COLOR_WHITE;
            s_btns[i].border_color = COLOR_WHITE;
        }
        s_btns[i].state = 0;
    }
}

/* 标题栏中的取消按钮(右上角)。 */
static Button_t s_cancel_btn;

void Keypad_Begin(const char *title, const char *prompt)
{
    uint8_t i;

    strncpy(s_title, title ? title : "INPUT", sizeof(s_title) - 1);
    s_title[sizeof(s_title) - 1] = 0;
    strncpy(s_prompt, prompt ? prompt : "Enter 6 digits", sizeof(s_prompt) - 1);
    s_prompt[sizeof(s_prompt) - 1] = 0;

    s_count = 0;
    memset(s_digits, 0, sizeof(s_digits));

    Kp_SetupBtns();

    LCD_Clear(COLOR_BLACK);

    /* 标题栏 */
    LCD_Fill(0, 0, LCD_W - 1, KP_TITLE_H - 1, 0x3186);
    LCD_ShowStringCN(8, 4, s_title, COLOR_WHITE, 0x3186, 1);

    /* 取消按钮(标题栏右上角) */
    s_cancel_btn.x = (uint16_t)(LCD_W - 100);
    s_cancel_btn.y = 4;
    s_cancel_btn.w = 92;
    s_cancel_btn.h = 24;
    s_cancel_btn.label = "取消";
    s_cancel_btn.bg_color = COLOR_RED;
    s_cancel_btn.press_color = COLOR_YELLOW;
    s_cancel_btn.text_color = COLOR_WHITE;
    s_cancel_btn.border_color = COLOR_WHITE;
    s_cancel_btn.state = 0;
    Button_Draw(&s_cancel_btn);

    /* 提示 */
    LCD_ShowStringCN(8, KP_TITLE_H + 2, s_prompt, COLOR_CYAN, COLOR_BLACK, 1);

    /* 输入格 */
    Kp_DrawCells();

    /* 键盘按键 */
    for (i = 0; i < KP_N_BTNS; i++)
        Button_Draw(&s_btns[i]);
}

KpResult Keypad_Scan(uint8_t touch_down, uint16_t tx, uint16_t ty, char out[KP_DIGITS + 1])
{
    uint8_t i;

    /* 取消键优先 */
    if (Button_Scan(&s_cancel_btn, touch_down, tx, ty))
        return KP_CANCEL;

    for (i = 0; i < KP_N_BTNS; i++)
    {
        if (Button_Scan(&s_btns[i], touch_down, tx, ty))
        {
            if (i == 12)   /* 确认 */
            {
                if (s_count == KP_DIGITS)
                {
                    memcpy(out, s_digits, KP_DIGITS);
                    out[KP_DIGITS] = 0;
                    return KP_DONE;
                }
                /* 位数不足: 闪烁空格 */
                Kp_DrawCells();
            }
            else if (i == 11)   /* 重输 */
            {
                s_count = 0;
                memset(s_digits, 0, sizeof(s_digits));
                Kp_DrawCells();
            }
            else if (i == 9)   /* 退格(<) */
            {
                if (s_count > 0)
                {
                    s_count--;
                    s_digits[s_count] = 0;
                    Kp_DrawCells();
                }
            }
            else   /* 数字0..9 */
            {
                char d;
                if (i == 10)      d = '0';
                else              d = (char)('1' + i);  /* i=0..8 -> '1'..'9' */

                if (s_count < KP_DIGITS)
                {
                    s_digits[s_count++] = d;
                    Kp_DrawCells();
                }
            }
            return KP_NONE;
        }
    }
    return KP_NONE;
}

uint8_t Keypad_Count(void)
{
    return s_count;
}

/* 从外部来源(如语音识别)注入一位数字。 */
uint8_t Keypad_InjectDigit(char d)
{
    if (s_count >= KP_DIGITS) return 0;
    if (d < '0' || d > '9') return 0;
    s_digits[s_count++] = d;
    Kp_DrawCells();
    return 1;
}

/* 清除所有已输入数字。 */
void Keypad_ClearAll(void)
{
    s_count = 0;
    memset(s_digits, 0, sizeof(s_digits));
    Kp_DrawCells();
}

/* 6位数字已输入满返回1。 */
uint8_t Keypad_IsFull(void)
{
    return (s_count >= KP_DIGITS) ? 1 : 0;
}

/* 将当前缓冲区复制到out[7]; 返回位数。 */
uint8_t Keypad_GetCode(char out[KP_DIGITS + 1])
{
    if (out)
    {
        memcpy(out, s_digits, KP_DIGITS);
        out[KP_DIGITS] = 0;
    }
    return s_count;
}
