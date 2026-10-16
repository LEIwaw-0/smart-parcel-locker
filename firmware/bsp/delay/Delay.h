#ifndef __DELAY_H
#define __DELAY_H

#include "stm32f4xx.h"  // 根据你的芯片，F4系列核心头文件

// 函数声明
void delay_ms(uint32_t ms);
void delay_us(uint32_t us);

#endif
