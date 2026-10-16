/******************************************************
**  keypad.h - on-screen 6-digit numeric keypad widget
**
**  Modal screen for entering 6 ASCII digits. Layout on
**  the 480x320 LCD:
**    - title bar  (top 32 px)
**    - prompt row (next 18 px)
**    - 6-cell input display (32x48 each, 12 px apart)
**    - 4x3 button grid  (digits 1-9, back, 0, OK)
**    - CANCEL button (top-right of title bar)
**
**  Usage:
**    Keypad_Begin(title, prompt);
**    for(;;) {
**        r = Keypad_Scan(touch_down, lx, ly, buf);
**        if (r != KP_NONE) break;
**    }
**    // r==KP_DONE  -> buf保存6位数字+结束符
**    // r==KP_CANCEL-> 用户取消
******************************************************/
#ifndef __KEYPAD_H
#define __KEYPAD_H

#include "stm32f4xx.h"
#include "button.h"

#define KP_DIGITS   6

typedef enum {
    KP_NONE   = 0,   /* still inputting                  */
    KP_DONE   = 1,   /* 输入6位后按确定 */
    KP_CANCEL = 2    /* CANCEL pressed                    */
} KpResult;

/* 绘制完整键盘界面并重置输入缓冲区。 */
void Keypad_Begin(const char *title, const char *prompt);

/* 轮询键盘。输入满6位按确定返回KP_DONE, 按取消返回KP_CANCEL。KP_DONE时out[]填充6位ASCII数字+结束符(大小>=7)。 */
KpResult Keypad_Scan(uint8_t touch_down, uint16_t tx, uint16_t ty, char out[KP_DIGITS + 1]);

/* 当前已输入数字位数(0..6)。用于实时状态显示。 */
uint8_t Keypad_Count(void);

/* 从外部来源(如语音识别)注入一位数字。未满则追加到缓冲区;刷新显示格。成功返回1,缓冲区已满返回0。 */
uint8_t Keypad_InjectDigit(char d);

/* Clear all entered digits (the "re-enter" / 重输 button). */
void Keypad_ClearAll(void);

/* 6位数字已输入满返回1,否则0。 */
uint8_t Keypad_IsFull(void);

/* 将当前缓冲区(6位ASCII数字+结束符)复制到out[7]。返回当前位数。 */
uint8_t Keypad_GetCode(char out[KP_DIGITS + 1]);

#endif /* __KEYPAD_H */
