#ifndef __TOUCH_H
#define __TOUCH_H

#include "stm32f4xx.h"
#include "lcd.h"

/*
 * XPT2046 电阻触摸驱动 (软件SPI)
 *   T_CS  = PA0 (推挽输出)      T_IRQ = PA1 (上拉输入, 低电平=按下)
 *   T_CLK = PE7 (推挽输出)      T_DIN = PE8 (推挽输出)      T_DO = PE9 (上拉输入)
 *
 * 横屏480x320: 采用厂家 touchtype=1 方案(交换X/Y读取命令), 线性变换用正系数
 *   屏幕坐标 = fac * raw + off
 * 参数先用典型估算值, 可按实测raw范围在宏里微调
 */

/* 触摸校准线性参数 (横屏 touchtype=1, 正系数) */
#define TOUCH_XFAC    (-0.1311f)
#define TOUCH_YFAC    (-0.0836f)
#define TOUCH_XOFF    513
#define TOUCH_YOFF    335

void    Touch_Init(void);                                   /* 初始化触摸GPIO */
uint8_t Touch_Scan(uint16_t *sx, uint16_t *sy, uint16_t *rx, uint16_t *ry); /* 扫描, 1=按下, sx/sy=屏幕坐标, rx/ry=原始AD(可传0) */
uint8_t Touch_ReadRaw(uint16_t *rx, uint16_t *ry);         /* 读原始AD值(已按touchtype交换), 1=成功 */

#endif
