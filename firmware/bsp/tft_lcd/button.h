#ifndef __BUTTON_H
#define __BUTTON_H

#include "lcd.h"

/* A rectangular touch button with a centered ASCII label.
   Uses the 5x7 font at size 2 (12x16 px per character). */

typedef struct {
    uint16_t x, y;           /* top-left corner        */
    uint16_t w, h;           /* width / height         */
    const char *label;       /* ASCII label (may be 0) */
    uint16_t text_color;     /* label color            */
    uint16_t bg_color;       /* released fill color    */
    uint16_t press_color;    /* pressed fill color     */
    uint16_t border_color;   /* frame color            */
    uint8_t  state;          /* 0 = released, 1 = pressed (internal) */
} Button_t;

/* Draw the button in its current state. */
void Button_Draw(Button_t *b);

/* Touch scan. Call every loop with the current touch sample:
     touch_down = XPT2046_TouchDown()
     tx, ty     = last valid mapped LCD coords
   Returns 1 on a click = finger pressed inside then lifted. */
uint8_t Button_Scan(Button_t *b, uint8_t touch_down, uint16_t tx, uint16_t ty);

#endif
