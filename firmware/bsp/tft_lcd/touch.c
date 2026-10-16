/******************************************************
**  XPT2046 电阻触摸驱动 (软件SPI)
**  引脚: T_CS=PA0  T_IRQ=PA1  T_CLK=PE7  T_DIN=PE8  T_DO=PE9
**  移植自厂家 Demo_STM32F407ZGT6_Software_SPI/TOUCH/touch.c
**  - 厂家SPI时序(发命令后等6us转换, 16位读取取高12位)
**  - 5次采样排序去极值平均 + 二次采样偏差滤波
**  - 横屏 touchtype=1: 交换X/Y读取命令(CMD_RDX=0x90, CMD_RDY=0xD0)
**  - 线性变换: 屏幕坐标 = fac * raw + off (正系数)
**  去掉厂家24CXX EEPROM存储, 参数用touch.h宏
******************************************************/
#include "touch.h"
#include "delay.h"

/* ---------------- 引脚操作 ---------------- */
#define T_CS(x)    GPIO_WriteBit(GPIOA, GPIO_Pin_0, (BitAction)(x))
#define T_IRQ()    GPIO_ReadInputDataBit(GPIOA, GPIO_Pin_1)
#define T_CLK(x)   GPIO_WriteBit(GPIOE, GPIO_Pin_7, (BitAction)(x))
#define T_DIN(x)   GPIO_WriteBit(GPIOE, GPIO_Pin_8, (BitAction)(x))
#define T_DO()     GPIO_ReadInputDataBit(GPIOE, GPIO_Pin_9)

/* 横屏 touchtype=1: X/Y方向与屏幕相反, 交换命令(厂家TP_Get_Adjdata逻辑) */
#define CMD_RDX   0x90    /* 读"屏幕X"对应的AD通道 */
#define CMD_RDY   0xD0    /* 读"屏幕Y"对应的AD通道 */

#define READ_TIMES  5     /* 采样次数 */
#define LOST_VAL    1     /* 排序后丢弃的最大/最小值个数 */
#define ERR_RANGE   50    /* 二次采样允许偏差 */

void Touch_Init(void)
{
    GPIO_InitTypeDef g;

    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA | RCC_AHB1Periph_GPIOE, ENABLE);

    /* PA0=T_CS 推挽输出 */
    g.GPIO_Pin   = GPIO_Pin_0;
    g.GPIO_Mode  = GPIO_Mode_OUT;
    g.GPIO_OType = GPIO_OType_PP;
    g.GPIO_Speed = GPIO_Speed_50MHz;
    g.GPIO_PuPd  = GPIO_PuPd_UP;
    GPIO_Init(GPIOA, &g);
    T_CS(1);

    /* PA1=T_IRQ 上拉输入 */
    g.GPIO_Pin  = GPIO_Pin_1;
    g.GPIO_Mode = GPIO_Mode_IN;
    g.GPIO_PuPd = GPIO_PuPd_UP;
    GPIO_Init(GPIOA, &g);

    /* PE7=T_CLK PE8=T_DIN 推挽输出 */
    g.GPIO_Pin   = GPIO_Pin_7 | GPIO_Pin_8;
    g.GPIO_Mode  = GPIO_Mode_OUT;
    g.GPIO_OType = GPIO_OType_PP;
    g.GPIO_Speed = GPIO_Speed_50MHz;
    g.GPIO_PuPd  = GPIO_PuPd_UP;
    GPIO_Init(GPIOE, &g);
    T_CLK(0);
    T_DIN(0);

    /* PE9=T_DO 上拉输入 */
    g.GPIO_Pin  = GPIO_Pin_9;
    g.GPIO_Mode = GPIO_Mode_IN;
    g.GPIO_PuPd = GPIO_PuPd_UP;
    GPIO_Init(GPIOE, &g);
}

/* 向触摸IC写一个字节 (厂家TP_Write_Byte: 数据在TCLK=0设置, 上升沿锁存) */
static void TP_Write_Byte(uint8_t num)
{
    uint8_t count;
    for (count = 0; count < 8; count++)
    {
        T_DIN((num & 0x80) ? 1 : 0);
        num <<= 1;
        T_CLK(0);
        delay_us(1);
        T_CLK(1);      /* 上升沿有效 */
    }
}

/* 读AD转换值 (厂家TP_Read_AD: 高12位有效) */
static uint16_t TP_Read_AD(uint8_t cmd)
{
    uint8_t  count;
    uint16_t num = 0;

    T_CLK(0);
    T_DIN(0);
    T_CS(0);                       /* 选中触摸IC */
    TP_Write_Byte(cmd);            /* 发命令字 */
    delay_us(6);                   /* ADS7846转换时间最长6us */
    T_CLK(0); delay_us(1);
    T_CLK(1); delay_us(1);         /* 1个时钟清BUSY */
    T_CLK(0);

    for (count = 0; count < 16; count++)   /* 读16位, 只有高12位有效 */
    {
        num <<= 1;
        T_CLK(0); delay_us(1);    /* 下降沿数据移出 */
        T_CLK(1);
        if (T_DO()) num++;
    }
    num >>= 4;                     /* 取高12位 */
    T_CS(1);                       /* 释放片选 */
    return num;
}

/* 多次采样排序去极值平均 (厂家TP_Read_XOY) */
static uint16_t TP_Read_XOY(uint8_t cmd)
{
    uint16_t i, j;
    uint16_t buf[READ_TIMES];
    uint16_t sum = 0, temp;

    for (i = 0; i < READ_TIMES; i++) buf[i] = TP_Read_AD(cmd);

    for (i = 0; i < READ_TIMES - 1; i++)      /* 升序排序 */
        for (j = i + 1; j < READ_TIMES; j++)
            if (buf[i] > buf[j]) { temp = buf[i]; buf[i] = buf[j]; buf[j] = temp; }

    for (i = LOST_VAL; i < READ_TIMES - LOST_VAL; i++) sum += buf[i];
    temp = sum / (READ_TIMES - 2 * LOST_VAL);
    return temp;
}

/* 读一次X/Y (厂家TP_Read_XY) */
static uint8_t TP_Read_XY(uint16_t *x, uint16_t *y)
{
    *x = TP_Read_XOY(CMD_RDX);
    *y = TP_Read_XOY(CMD_RDY);
    return 1;
}

/* 连续读两次, 偏差<ERR_RANGE才接受 (厂家TP_Read_XY2) */
static uint8_t TP_Read_XY2(uint16_t *x, uint16_t *y)
{
    uint16_t x1, y1, x2, y2;

    if (!TP_Read_XY(&x1, &y1)) return 0;
    if (!TP_Read_XY(&x2, &y2)) return 0;

    if (((x2 <= x1 && x1 < x2 + ERR_RANGE) || (x1 <= x2 && x2 < x1 + ERR_RANGE)) &&
        ((y2 <= y1 && y1 < y2 + ERR_RANGE) || (y1 <= y2 && y2 < y1 + ERR_RANGE)))
    {
        *x = (x1 + x2) / 2;
        *y = (y1 + y2) / 2;
        return 1;
    }
    return 0;
}

/* 读原始AD值(已按touchtype交换命令), 1=成功 */
uint8_t Touch_ReadRaw(uint16_t *rx, uint16_t *ry)
{
    if (T_IRQ() != Bit_RESET) return 0;   /* 无触摸 */
    return TP_Read_XY2(rx, ry);
}

/* 扫描: 1=按下, sx/sy=屏幕坐标(已线性变换并限幅), rx/ry=原始AD(可传0) */
uint8_t Touch_Scan(uint16_t *sx, uint16_t *sy, uint16_t *rx, uint16_t *ry)
{
    uint16_t rawx, rawy;
    int32_t  vx, vy;

    if (T_IRQ() != Bit_RESET) return 0;   /* 无触摸 */
    if (!TP_Read_XY2(&rawx, &rawy)) return 0;

    if (rx) *rx = rawx;
    if (ry) *ry = rawy;

    vx = (int32_t)(TOUCH_XFAC * rawx + TOUCH_XOFF);
    vy = (int32_t)(TOUCH_YFAC * rawy + TOUCH_YOFF);

    if (vx < 0) vx = 0;  else if (vx >= LCD_W) vx = LCD_W - 1;
    if (vy < 0) vy = 0;  else if (vy >= LCD_H) vy = LCD_H - 1;

    *sx = (uint16_t)vx;
    *sy = (uint16_t)vy;
    return 1;
}
