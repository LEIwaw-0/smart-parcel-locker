/******************************************************
**  Touch button widget for the 4 inch SPI LCD.
**  Depends on lcd.c (LCD_Fill/DrawRect/ShowString).
******************************************************/
#include "button.h"
#include <string.h>

#define BTN_CHAR_W  12   /* 5x7 font * 2 */
#define BTN_CHAR_H  16

/* point-in-rectangle test */
static uint8_t Btn_InRect(Button_t *b, uint16_t x, uint16_t y)
{
    return (x >= b->x && x < b->x + b->w &&
            y >= b->y && y < b->y + b->h) ? 1 : 0;
}

/* render the button body, border and centered label */
static void Btn_Render(Button_t *b)
{
    uint16_t fill = b->state ? b->press_color : b->bg_color;
    uint16_t x2   = (uint16_t)(b->x + b->w - 1);
    uint16_t y2   = (uint16_t)(b->y + b->h - 1);

    LCD_Fill(b->x, b->y, x2, y2, fill);
    LCD_DrawRect(b->x, b->y, x2, y2, b->border_color);

    if (b->label)
    {
        /* All glyphs share a 16px line height: GBK chars 16 wide,
           ASCII 12 wide at size 2. */
        uint16_t len = LCD_StringCN_Width(b->label, 2);
        uint16_t tx  = (uint16_t)(b->x + (b->w - len) / 2);
        uint16_t ty  = (uint16_t)(b->y + (b->h - 16) / 2);
        LCD_ShowStringCN(tx, ty, b->label, b->text_color, fill, 2);
    }
}

void Button_Draw(Button_t *b)
{
    Btn_Render(b);
}

uint8_t Button_Scan(Button_t *b, uint8_t touch_down, uint16_t tx, uint16_t ty)
{
    uint8_t click = 0;

    if (touch_down && Btn_InRect(b, tx, ty))
    {
        if (!b->state) { b->state = 1; Btn_Render(b); }
    }
    else
    {
        /* finger up, or moved outside: release */
        if (b->state)
        {
            if (!touch_down) click = 1;   /* lifted on button = click */
            b->state = 0;
            Btn_Render(b);
        }
    }
    return click;
}
