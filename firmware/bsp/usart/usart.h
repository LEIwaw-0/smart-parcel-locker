#ifndef __USART_H
#define __USART_H

#include "stm32f4xx.h"
#include <stdio.h>

void Usart1_Init(uint32_t baud);   /* USART1 PA9(TX)/PA10(RX), 8N1 */

/* ===================== 裸输出层 =====================
 * 这五个函数只允许"读标志位 + 写数据寄存器 + 循环"：不取互斥量、不依赖
 * 调度器、不用堆。唯一的调用者应该是 app/hooks.c 里的断言/栈溢出/堆耗尽/
 * 硬故障四个钩子 —— 那时候互斥量和调度器恰恰是全机最不可信的东西。
 * 代价：无互斥（多处同时输出会字符交错）、阻塞。
 * =================================================== */
uint8_t Usart1_IsReady(void);                /* 初始化完成返回 1；未完成时 raw 输出被丢弃 */
void    Usart1_RawPutc(char c);              /* 单字节 */
void    Usart1_RawPuts(const char *s);       /* 字符串；'\n' 自动补 '\r' */
void    Usart1_RawPutHex32(uint32_t v);      /* 固定 10 字符，形如 0x1234ABCD */
void    Usart1_RawPutDec(uint32_t v);        /* 无符号十进制 */

#endif
