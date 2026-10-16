#ifndef __LCD_H
#define __LCD_H

#include "stm32f4xx.h"

#define LCD_W   480
#define LCD_H   320

#define COLOR_WHITE    0xFFFF
#define COLOR_BLACK    0x0000
#define COLOR_RED      0xF800
#define COLOR_GREEN    0x07E0
#define COLOR_BLUE     0x001F
#define COLOR_YELLOW   0xFFE0
#define COLOR_CYAN     0x07FF
#define COLOR_MAGENTA  0xF81F
#define COLOR_GRAY     0x8430
#define COLOR_ORANGE   0xFD20


void     LCD_WriteCmd(uint8_t cmd);
void     LCD_WriteData(uint8_t d);
void     LCD_Init(void);
uint16_t LCD_ReadID(void);
void     LCD_Backlight(uint8_t on);
void     LCD_SetWindow(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2);
void     LCD_Clear(uint16_t color);
void     LCD_DrawPoint(uint16_t x, uint16_t y, uint16_t color);
void     LCD_Fill(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color);
void     LCD_DrawLine(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color);
void     LCD_DrawRect(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color);
void     LCD_DrawCircle(uint16_t x0, uint16_t y0, uint16_t r, uint16_t color);
void     LCD_ShowChar(uint16_t x, uint16_t y, char ch, uint16_t fc, uint16_t bc, uint8_t size);
void     LCD_ShowString(uint16_t x, uint16_t y, const char *s, uint16_t fc, uint16_t bc, uint8_t size);
/* Show a 16x16 Chinese char (GBK 2-byte code). size: 1=16x16, 2=32x32. */
void     LCD_ShowCharCN(uint16_t x, uint16_t y, uint16_t gbk, uint16_t fc, uint16_t bc, uint8_t size);
/* Show a mixed GBK/ASCII string. GBK pairs render 16x16; ASCII as usual. */
void     LCD_ShowStringCN(uint16_t x, uint16_t y, const char *s, uint16_t fc, uint16_t bc, uint8_t size);
/* Pixel width of a mixed GBK/ASCII string. */
uint16_t LCD_StringCN_Width(const char *s, uint8_t size);

/* ---- UI polish: rounded shapes, gradient, scalable ASCII ---- */
/* Filled rounded rectangle. r=0 falls back to a plain rectangle. */
void     LCD_FillRound(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t r, uint16_t color);
/* Rounded rectangle outline. */
void     LCD_DrawRoundRect(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t r, uint16_t color);
/* Vertical RGB565 gradient fill (c1 at y1 -> c2 at y2). */
void     LCD_FillGradV(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t c1, uint16_t c2);
/* Scaled ASCII glyph from the 5x7 font. scale 1..8; cell = 6*scale x 8*scale. */
void     LCD_ShowCharBig(uint16_t x, uint16_t y, char ch, uint16_t fc, uint16_t bc, uint8_t scale);
void     LCD_ShowStringBig(uint16_t x, uint16_t y, const char *s, uint16_t fc, uint16_t bc, uint8_t scale);

/* ---- UI palette (RGB565) ---- */
#define COLOR_BG      0x1082   /* dark blue-grey background */
#define COLOR_PANEL   0x2945   /* card / panel fill        */
#define COLOR_PANEL2  0x39E7   /* lighter panel            */
#define COLOR_TEXT    0xEF7D   /* near-white text          */
#define COLOR_ACCENT  0x2D7F   /* cyan accent              */
#define COLOR_DIM     0x6B4D   /* dimmed text              */

#endif

