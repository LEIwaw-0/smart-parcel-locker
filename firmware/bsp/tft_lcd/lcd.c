/******************************************************
**  ST7796S 4.0英寸SPI液晶屏驱动 (软件SPI)
**  芯片: ST7796S, 480x320分辨率, RGB565
**  引脚: CS=PA4 DC=PB0 RST=PB1 BLK=PB2
**        SCK=PA5 MISO=PA6 MOSI=PA7
**  移植自厂家 Demo_STM32F407ZGT6_Software_SPI/LCD/lcd.c
******************************************************/
#include "lcd.h"
#include "delay.h"
#include "font.h"

/* ---------------- 引脚定义宏 ---------------- */
#define LCD_CS(x)    GPIO_WriteBit(GPIOA, GPIO_Pin_4, (BitAction)(x))
#define LCD_DC(x)    GPIO_WriteBit(GPIOB, GPIO_Pin_0, (BitAction)(x))
#define LCD_RST(x)   GPIO_WriteBit(GPIOB, GPIO_Pin_1, (BitAction)(x))
#define LCD_BLK(x)   GPIO_WriteBit(GPIOB, GPIO_Pin_2, (BitAction)(x))

/* 软件SPI引脚宏 */
#define LCD_SCK(x)   GPIO_WriteBit(GPIOA, GPIO_Pin_5, (BitAction)(x))
#define LCD_MOSI(x)  GPIO_WriteBit(GPIOA, GPIO_Pin_7, (BitAction)(x))
#define LCD_MISO()   GPIO_ReadInputDataBit(GPIOA, GPIO_Pin_6)

/* 默认显示方向: MADCTL=0x68 */

/* ---------------- 软件SPI (移植自厂家SPIv_WriteData) ---------------- */
static void LCD_SPI_Wr(uint8_t d)
{
    uint8_t i;
    for (i = 0; i < 8; i++)
    {
        LCD_MOSI((d & 0x80) ? 1 : 0);
        d <<= 1;
        LCD_SCK(0);
        LCD_SCK(1);   /* 上升沿采样 */
    }
}

static uint8_t LCD_SPI_Rd(void)
{
    uint8_t i, d = 0;
    for (i = 0; i < 8; i++)
    {
        LCD_SCK(0);
        LCD_SCK(1);
        d <<= 1;
        if (LCD_MISO()) d |= 1;
    }
    return d;
}

/* ---------------- 命令/数据写 (移植自厂家LCD_WR_REG/LCD_WR_DATA) ---------------- */
void LCD_WriteCmd(uint8_t cmd)
{
    LCD_CS(0);
    LCD_DC(0);      /* DC=0 表示命令 */
    LCD_SPI_Wr(cmd);
    LCD_CS(1);
}

void LCD_WriteData(uint8_t d)
{
    LCD_CS(0);
    LCD_DC(1);      /* DC=1 表示数据 */
    LCD_SPI_Wr(d);
    LCD_CS(1);
}

/* ---------------- GPIO初始化 ---------------- */
static void LCD_GPIO_Init(void)
{
    GPIO_InitTypeDef g;

    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA | RCC_AHB1Periph_GPIOB, ENABLE);

    /* PB0=DC PB1=RST PB2=BLK 输出推挽 */
    g.GPIO_Pin   = GPIO_Pin_0 | GPIO_Pin_1 | GPIO_Pin_2;
    g.GPIO_Mode  = GPIO_Mode_OUT;
    g.GPIO_OType = GPIO_OType_PP;
    g.GPIO_Speed = GPIO_Speed_50MHz;
    g.GPIO_PuPd  = GPIO_PuPd_UP;
    GPIO_Init(GPIOB, &g);

    /* PA4=CS PA5=SCK PA7=MOSI 输出推挽 */
    g.GPIO_Pin   = GPIO_Pin_4 | GPIO_Pin_5 | GPIO_Pin_7;
    GPIO_Init(GPIOA, &g);

    /* PA6=MISO 输入上拉 */
    g.GPIO_Pin  = GPIO_Pin_6;
    g.GPIO_Mode = GPIO_Mode_IN;
    g.GPIO_PuPd = GPIO_PuPd_UP;
    GPIO_Init(GPIOA, &g);

    /* 空闲状态 */
    LCD_CS(1);
    LCD_DC(1);
    LCD_SCK(0);
    LCD_MOSI(0);
}

/* ---------------- 硬件复位 (移植自厂家LCD_RESET) ---------------- */
static void LCD_Reset(void)
{
    LCD_RST(0);
    delay_ms(100);
    LCD_RST(1);
    delay_ms(50);
}

/* ---------------- ST7796S初始化 (移植自厂家官方代码) ---------------- */
void LCD_Init(void)
{
    LCD_GPIO_Init();
    LCD_Reset();

    /* ===== ST7796S初始化序列 (按原厂，一步一步来) ===== */
    LCD_WriteCmd(0xF0); LCD_WriteData(0xC3);   /* 命令扩展2 partI */
    LCD_WriteCmd(0xF0); LCD_WriteData(0x96);   /* 命令扩展2 partII */

    LCD_WriteCmd(0x36); LCD_WriteData(0x68);   /* MADCTL初始值 */
    LCD_WriteCmd(0x3A); LCD_WriteData(0x05);   /* 数据格式: 16位RGB565 */

    LCD_WriteCmd(0xB0); LCD_WriteData(0x80);
    LCD_WriteCmd(0xB6); LCD_WriteData(0x00); LCD_WriteData(0x02);
    LCD_WriteCmd(0xB5); LCD_WriteData(0x02); LCD_WriteData(0x03); LCD_WriteData(0x00); LCD_WriteData(0x04);
    LCD_WriteCmd(0xB1); LCD_WriteData(0x80); LCD_WriteData(0x10);
    LCD_WriteCmd(0xB4); LCD_WriteData(0x00);
    LCD_WriteCmd(0xB7); LCD_WriteData(0xC6);
    LCD_WriteCmd(0xC5); LCD_WriteData(0x24);
    LCD_WriteCmd(0xE4); LCD_WriteData(0x31);
    LCD_WriteCmd(0xE8);
    LCD_WriteData(0x40); LCD_WriteData(0x8A); LCD_WriteData(0x00); LCD_WriteData(0x00);
    LCD_WriteData(0x29); LCD_WriteData(0x19); LCD_WriteData(0xA5); LCD_WriteData(0x33);

    LCD_WriteCmd(0xC2);   /* 无参数命令 */
    LCD_WriteCmd(0xA7);   /* 无参数命令 */

    /* 正gamma校正 */
    LCD_WriteCmd(0xE0);
    LCD_WriteData(0xF0); LCD_WriteData(0x09); LCD_WriteData(0x13); LCD_WriteData(0x12);
    LCD_WriteData(0x12); LCD_WriteData(0x2B); LCD_WriteData(0x3C); LCD_WriteData(0x44);
    LCD_WriteData(0x4B); LCD_WriteData(0x1B); LCD_WriteData(0x18); LCD_WriteData(0x17);
    LCD_WriteData(0x1D); LCD_WriteData(0x21);

    /* 负gamma校正 */
    LCD_WriteCmd(0xE1);
    LCD_WriteData(0xF0); LCD_WriteData(0x09); LCD_WriteData(0x13); LCD_WriteData(0x0C);
    LCD_WriteData(0x0D); LCD_WriteData(0x27); LCD_WriteData(0x3B); LCD_WriteData(0x44);
    LCD_WriteData(0x4D); LCD_WriteData(0x0B); LCD_WriteData(0x17); LCD_WriteData(0x17);
    LCD_WriteData(0x1D); LCD_WriteData(0x21);

    LCD_WriteCmd(0x36); LCD_WriteData(0xEC);   /* MADCTL第二次写 */
    LCD_WriteCmd(0xF0); LCD_WriteData(0xC3);   /* 命令扩展partI 恢复原值(实测才work) */
    LCD_WriteCmd(0xF0); LCD_WriteData(0x69);   /* 命令扩展2 partII */

    LCD_WriteCmd(0x13);   /* 进入正常显示模式 */
    LCD_WriteCmd(0x11);   /* 退出睡眠 */
    delay_ms(120);
    LCD_WriteCmd(0x29);   /* 开显示 */
    delay_ms(20);

    LCD_BLK(1);                        /* 开背光 */
    LCD_Clear(COLOR_WHITE);           /* 先清一次屏(0xEC方向, 等display on稳定) */

    /* 行列：480x320 需在0x29稳定后写0x36，否则MV位异常导致地址错位 */
    LCD_WriteCmd(0x36); LCD_WriteData(0x28);
    delay_ms(20);
    LCD_Clear(COLOR_WHITE);           /* 方向修正后再次清屏 */
}

uint16_t LCD_ReadID(void)
{
    uint16_t id;
    LCD_CS(0);
    LCD_DC(0);
    LCD_SPI_Wr(0xD3);
    LCD_DC(1);
    (void)LCD_SPI_Rd();
    (void)LCD_SPI_Rd();
    id  = (uint16_t)LCD_SPI_Rd() << 8;
    id |= LCD_SPI_Rd();
    LCD_CS(1);
    return id;
}

/* ---------------- 背光控制 ---------------- */
void LCD_Backlight(uint8_t on)
{
    LCD_BLK(on ? 1 : 0);
}

/* ---------------- 设置显示区域 (移植自厂家LCD_SetWindows) ---------------- */
void LCD_SetWindow(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
{
        LCD_WriteCmd(0x2A);   /* 列地址设置 */
    LCD_WriteData((uint8_t)(x1 >> 8));
    LCD_WriteData((uint8_t)(x1 & 0xFF));
    LCD_WriteData((uint8_t)(x2 >> 8));
    LCD_WriteData((uint8_t)(x2 & 0xFF));
    LCD_WriteCmd(0x2B);   /* 行地址设置 */
    LCD_WriteData((uint8_t)(y1 >> 8));
    LCD_WriteData((uint8_t)(y1 & 0xFF));
    LCD_WriteData((uint8_t)(y2 >> 8));
    LCD_WriteData((uint8_t)(y2 & 0xFF));
    LCD_WriteCmd(0x2C);   /* 写GRAM */
}

/* ---------------- 画点 ---------------- */
void LCD_DrawPoint(uint16_t x, uint16_t y, uint16_t color)
{
    if (x >= LCD_W || y >= LCD_H) return;
    LCD_SetWindow(x, y, x, y);
    LCD_CS(0); LCD_DC(1);
    LCD_SPI_Wr((uint8_t)(color >> 8));
    LCD_SPI_Wr((uint8_t)(color & 0xFF));
    LCD_CS(1);
}

/* ---------------- 清屏 ---------------- */
void LCD_Clear(uint16_t color)
{
    LCD_Fill(0, 0, LCD_W - 1, LCD_H - 1, color);
}

/* ---------------- 区域填充 (连续写, 不每个点翻转CS) ---------------- */
void LCD_Fill(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color)
{
    uint32_t n;
    if (x2 >= LCD_W) x2 = LCD_W - 1;
    if (y2 >= LCD_H) y2 = LCD_H - 1;
    if (x1 >= LCD_W || y1 >= LCD_H || x1 > x2 || y1 > y2) return;
    n = (uint32_t)(x2 - x1 + 1) * (y2 - y1 + 1);

    LCD_SetWindow(x1, y1, x2, y2);
    LCD_CS(0); LCD_DC(1);
    while (n--) {
        LCD_SPI_Wr((uint8_t)(color >> 8));
        LCD_SPI_Wr((uint8_t)(color & 0xFF));
    }
    LCD_CS(1);
}

/* ---------------- 画线 (Bresenham算法) ---------------- */
void LCD_DrawLine(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color)
{
    int16_t dx = (x2 > x1) ? (int16_t)(x2 - x1) : (int16_t)(x1 - x2);
    int16_t dy = (y2 > y1) ? (int16_t)(y2 - y1) : (int16_t)(y1 - y2);
    int16_t sx = (x1 < x2) ? 1 : -1;
    int16_t sy = (y1 < y2) ? 1 : -1;
    int16_t err = dx - dy;
    int16_t e2;

    for (;;) {
        LCD_DrawPoint(x1, y1, color);
        if (x1 == x2 && y1 == y2) break;
        e2 = (int16_t)(err * 2);
        if (e2 > -dy) { err -= dy; x1 = (int16_t)(x1 + sx); }
        if (e2 <  dx) { err += dx; y1 = (int16_t)(y1 + sy); }
    }
}

/* ---------------- 画矩形框 ---------------- */
void LCD_DrawRect(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color)
{
    LCD_DrawLine(x1, y1, x2, y1, color);
    LCD_DrawLine(x2, y1, x2, y2, color);
    LCD_DrawLine(x2, y2, x1, y2, color);
    LCD_DrawLine(x1, y2, x1, y1, color);
}

/* ---------------- 画圆 (中点画圆法) ---------------- */
void LCD_DrawCircle(uint16_t x0, uint16_t y0, uint16_t r, uint16_t color)
{
    int32_t f = 1 - r;
    int32_t ddx = 1;
    int32_t ddy = -2 * (int32_t)r;
    int32_t x = 0, y = r;

    while (x <= y) {
        LCD_DrawPoint((uint16_t)(x0 + x), (uint16_t)(y0 + y), color);
        LCD_DrawPoint((uint16_t)(x0 - x), (uint16_t)(y0 + y), color);
        LCD_DrawPoint((uint16_t)(x0 + x), (uint16_t)(y0 - y), color);
        LCD_DrawPoint((uint16_t)(x0 - x), (uint16_t)(y0 - y), color);
        LCD_DrawPoint((uint16_t)(x0 + y), (uint16_t)(y0 + x), color);
        LCD_DrawPoint((uint16_t)(x0 - y), (uint16_t)(y0 + x), color);
        LCD_DrawPoint((uint16_t)(x0 + y), (uint16_t)(y0 - x), color);
        LCD_DrawPoint((uint16_t)(x0 - y), (uint16_t)(y0 - x), color);
        if (f <= 0) { f += ddx; ddx += 2; x++; }
        else        { f += ddx + ddy; ddx += 2; ddy += 2; x++; y--; }
    }
}

/* ---------------- 显示单个字符 (5x7字库, 列扫描, bit0在上) ---------------- */
/* size: 1=6x8原始, 2=12x16放大2倍 */
void LCD_ShowChar(uint16_t x, uint16_t y, char ch, uint16_t fc, uint16_t bc, uint8_t size)
{
    uint8_t  s = (size >= 2) ? 2 : 1;
    uint8_t  row, col, k, t;
    uint16_t i = 0;
    uint16_t buf[6 * 8 * 2 * 2];
    uint16_t len = (uint16_t)(6 * s) * (8 * s);
    const uint8_t *f;

    if (ch < ' ' || ch > '~') ch = '?';
    f = &ASCII5x7[(ch - ' ') * 5];

    /* 先构造像素缓冲区 (按行扫描) */
    for (row = 0; row < 8; row++) {
        for (k = 0; k < s; k++) {
            for (col = 0; col < 5; col++) {
                uint16_t c = ((f[col] >> row) & 0x01) ? fc : bc;
                for (t = 0; t < s; t++) buf[i++] = c;
            }
            for (t = 0; t < s; t++) buf[i++] = bc;  /* 字符间距1列 */
        }
    }

    /* 连续写入像素 (CS保持低电平写完整个字符) */
    LCD_SetWindow(x, y, (uint16_t)(x + 6 * s - 1), (uint16_t)(y + 8 * s - 1));
    LCD_CS(0); LCD_DC(1);
    for (i = 0; i < len; i++) {
        LCD_SPI_Wr((uint8_t)(buf[i] >> 8));
        LCD_SPI_Wr((uint8_t)(buf[i] & 0xFF));
    }
    LCD_CS(1);
}

/* ---------------- 显示字符串 ---------------- */
void LCD_ShowString(uint16_t x, uint16_t y, const char *s, uint16_t fc, uint16_t bc, uint8_t size)
{
    uint8_t w = (size >= 2) ? 12 : 6;
    while (*s) {
        LCD_ShowChar(x, y, *s, fc, bc, size);
        x = (uint16_t)(x + w);
        s++;
    }
}

/* ---------------- show 16x16 Chinese (GBK) ---------------- */
#include "hzfont.h"
void LCD_ShowCharCN(uint16_t x, uint16_t y, uint16_t gbk, uint16_t fc, uint16_t bc, uint8_t size)
{
    const uint8_t *bits = HZ_Lookup(gbk);
    uint8_t row, col, bit, t;
    uint16_t buf[16 * 16];   /* 256 uint16_t = 512 bytes, safe on stack */
    uint16_t i = 0;
    uint8_t s = (size >= 2) ? 2 : 1;

    if (!bits) {  /* fallback: show ? */
        LCD_ShowChar(x, y, '?', fc, bc, size);
        return;
    }

    /* build pixel buffer (no scaling, 16x16) */
    for (row = 0; row < 16; row++) {
        for (col = 0; col < 2; col++) {
            uint8_t byte = bits[row * 2 + col];
            for (bit = 0; bit < 8; bit++) {
                buf[i++] = ((byte << bit) & 0x80) ? fc : bc;
            }
        }
    }

    if (s == 1) {
        /* 1:1 scale, send directly */
        LCD_SetWindow(x, y, (uint16_t)(x + 15), (uint16_t)(y + 15));
        LCD_CS(0); LCD_DC(1);
        for (i = 0; i < 256; i++) {
            LCD_SPI_Wr((uint8_t)(buf[i] >> 8));
            LCD_SPI_Wr((uint8_t)(buf[i] & 0xFF));
        }
        LCD_CS(1);
    } else {
        /* 2x scale: each pixel repeated 2x2. buf2x needs 32*32=1024 uint16_t = 2KB.
           Use row-by-row to avoid large stack buffer. */
        LCD_SetWindow(x, y, (uint16_t)(x + 31), (uint16_t)(y + 31));
        LCD_CS(0); LCD_DC(1);
        for (row = 0; row < 16; row++) {
            for (t = 0; t < 2; t++) {  /* vertical: repeat each row twice */
                for (col = 0; col < 16; col++) {
                    uint16_t c = buf[row * 16 + col];
                    LCD_SPI_Wr((uint8_t)(c >> 8));      /* pixel 1 */
                    LCD_SPI_Wr((uint8_t)(c & 0xFF));
                    LCD_SPI_Wr((uint8_t)(c >> 8));      /* pixel 2 */
                    LCD_SPI_Wr((uint8_t)(c & 0xFF));
                }
            }
        }
        LCD_CS(1);
    }
}

/* ---------------- show mixed GBK/ASCII string ---------------- */
/* Chinese glyphs are ALWAYS 16x16 (baseline aligned to 16px).
   size=1: ASCII 6x8;  size=2: ASCII 12x16. All glyphs share the same
   16px line height at size=2, so digits and Chinese text stay aligned. */
void LCD_ShowStringCN(uint16_t x, uint16_t y, const char *s, uint16_t fc, uint16_t bc, uint8_t size)
{
    uint8_t aw = (size >= 2) ? 12 : 6;
    while (*s) {
        uint8_t b = (uint8_t)*s;
        if (b >= 0x81 && b <= 0xFE && (uint8_t)(*(s+1)) >= 0x40) {
            /* GBK double-byte: always 16x16, vertically centred to the
               ASCII line height */
            uint16_t gbk = ((uint16_t)b << 8) | (uint8_t)(*(s+1));
            LCD_ShowCharCN(x, y, gbk, fc, bc, 1);
            x = (uint16_t)(x + 16);
            s += 2;
        } else {
            /* ASCII */
            LCD_ShowChar(x, y, (char)b, fc, bc, size);
            x = (uint16_t)(x + aw);
            s++;
        }
    }
}

/* Pixel width a mixed string would occupy (16 per GBK char, 6/12 ASCII). */
uint16_t LCD_StringCN_Width(const char *s, uint8_t size)
{
    uint16_t w = 0;
    uint8_t aw = (size >= 2) ? 12 : 6;
    while (*s) {
        uint8_t b = (uint8_t)*s;
        if (b >= 0x81 && b <= 0xFE && (uint8_t)(*(s+1)) >= 0x40) {
            w = (uint16_t)(w + 16);
            s += 2;
        } else {
            w = (uint16_t)(w + aw);
            s++;
        }
    }
    return w;
}

/* ================= UI polish helpers ================= */

/* Integer square root, used by the rounded-rectangle corner math. */
static uint16_t isqrt16(uint32_t v)
{
    uint32_t x = 0, b = 0x8000;
    while (b) {
        uint32_t t = x | b;
        if (t * t <= v) x = t;
        b >>= 1;
    }
    return (uint16_t)x;
}

/* One 90-degree outline arc centred at (cx,cy); quadrant chosen by qx/qy. */
static void corner_arc(int32_t cx, int32_t cy, int32_t r, int32_t qx, int32_t qy, uint16_t color)
{
    int32_t x = 0, y = r, f = 1 - r;
    while (x <= y) {
        LCD_DrawPoint((uint16_t)(cx + qx * x), (uint16_t)(cy + qy * y), color);
        LCD_DrawPoint((uint16_t)(cx + qx * y), (uint16_t)(cy + qy * x), color);
        if (f <= 0) { f += 2 * x + 3; x++; }
        else        { f += 2 * (x - y) + 5; x++; y--; }
    }
}

/* Filled rounded rectangle: full-width middle bands plus four corner rows
   whose horizontal span is shortened by the circle chord. */
void LCD_FillRound(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t r, uint16_t color)
{
    int32_t i, dy, dx;

    if (r > (uint16_t)((x2 - x1 + 1) / 2)) r = (uint16_t)((x2 - x1 + 1) / 2);
    if (r > (uint16_t)((y2 - y1 + 1) / 2)) r = (uint16_t)((y2 - y1 + 1) / 2);
    if (r == 0) { LCD_Fill(x1, y1, x2, y2, color); return; }

    LCD_Fill(x1, (uint16_t)(y1 + r), x2, (uint16_t)(y2 - r), color);
    LCD_Fill((uint16_t)(x1 + r), y1, (uint16_t)(x2 - r), y2, color);

    for (i = 0; i < (int32_t)r; i++) {
        dy = (int32_t)r - i;
        dx = (int32_t)isqrt16((uint32_t)((int32_t)r * (int32_t)r - dy * dy));
        LCD_Fill((uint16_t)(x1 + r - dx), (uint16_t)(y1 + i),
                 (uint16_t)(x2 - r + dx), (uint16_t)(y1 + i), color);
        LCD_Fill((uint16_t)(x1 + r - dx), (uint16_t)(y2 - i),
                 (uint16_t)(x2 - r + dx), (uint16_t)(y2 - i), color);
    }
}

/* Rounded rectangle outline: four straight edges plus four quarter arcs. */
void LCD_DrawRoundRect(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t r, uint16_t color)
{
    if (r > (uint16_t)((x2 - x1 + 1) / 2)) r = (uint16_t)((x2 - x1 + 1) / 2);
    if (r > (uint16_t)((y2 - y1 + 1) / 2)) r = (uint16_t)((y2 - y1 + 1) / 2);
    if (r == 0) { LCD_DrawRect(x1, y1, x2, y2, color); return; }

    LCD_DrawLine((uint16_t)(x1 + r), y1, (uint16_t)(x2 - r), y1, color);
    LCD_DrawLine((uint16_t)(x1 + r), y2, (uint16_t)(x2 - r), y2, color);
    LCD_DrawLine(x1, (uint16_t)(y1 + r), x1, (uint16_t)(y2 - r), color);
    LCD_DrawLine(x2, (uint16_t)(y1 + r), x2, (uint16_t)(y2 - r), color);
    corner_arc((int32_t)x1 + r, (int32_t)y1 + r, (int32_t)r, -1, -1, color);
    corner_arc((int32_t)x2 - r, (int32_t)y1 + r, (int32_t)r,  1, -1, color);
    corner_arc((int32_t)x1 + r, (int32_t)y2 - r, (int32_t)r, -1,  1, color);
    corner_arc((int32_t)x2 - r, (int32_t)y2 - r, (int32_t)r,  1,  1, color);
}

/* Vertical RGB565 gradient, interpolated per scan line. */
void LCD_FillGradV(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t c1, uint16_t c2)
{
    int32_t h = (int32_t)y2 - (int32_t)y1;
    int32_t y;
    int32_t r1, g1, b1, r2, g2, b2;

    if (h <= 0) { LCD_Fill(x1, y1, x2, y2, c1); return; }
    r1 = (c1 >> 11) & 0x1F; g1 = (c1 >> 5) & 0x3F; b1 = c1 & 0x1F;
    r2 = (c2 >> 11) & 0x1F; g2 = (c2 >> 5) & 0x3F; b2 = c2 & 0x1F;
    for (y = 0; y <= h; y++) {
        uint16_t r = (uint16_t)(r1 + (r2 - r1) * y / h);
        uint16_t g = (uint16_t)(g1 + (g2 - g1) * y / h);
        uint16_t b = (uint16_t)(b1 + (b2 - b1) * y / h);
        LCD_Fill(x1, (uint16_t)(y1 + y), x2, (uint16_t)(y1 + y),
                 (uint16_t)((r << 11) | (g << 5) | b));
    }
}

/* Magnified ASCII glyph. Sends one glyph row at a time so the stack cost
   stays at 48 uint16_t regardless of the scale factor. */
void LCD_ShowCharBig(uint16_t x, uint16_t y, char ch, uint16_t fc, uint16_t bc, uint8_t scale)
{
    uint8_t row, col, k, t;
    uint16_t buf[6 * 8];
    uint16_t n;
    const uint8_t *f;

    if (scale < 1) scale = 1;
    if (scale > 8) scale = 8;
    if (ch < ' ' || ch > '~') ch = '?';
    f = &ASCII5x7[(ch - ' ') * 5];

    LCD_SetWindow(x, y, (uint16_t)(x + 6 * scale - 1), (uint16_t)(y + 8 * scale - 1));
    LCD_CS(0); LCD_DC(1);
    for (row = 0; row < 8; row++) {
        n = 0;
        for (col = 0; col < 5; col++) {
            uint16_t c = ((f[col] >> row) & 0x01) ? fc : bc;
            for (t = 0; t < scale; t++) buf[n++] = c;
        }
        for (t = 0; t < scale; t++) buf[n++] = bc;   /* inter-character gap */
        for (k = 0; k < scale; k++) {                /* vertical magnification */
            for (t = 0; t < n; t++) {
                LCD_SPI_Wr((uint8_t)(buf[t] >> 8));
                LCD_SPI_Wr((uint8_t)(buf[t] & 0xFF));
            }
        }
    }
    LCD_CS(1);
}

void LCD_ShowStringBig(uint16_t x, uint16_t y, const char *s, uint16_t fc, uint16_t bc, uint8_t scale)
{
    if (scale < 1) scale = 1;
    while (*s) {
        LCD_ShowCharBig(x, y, *s, fc, bc, scale);
        x = (uint16_t)(x + 6 * scale);
        s++;
    }
}

