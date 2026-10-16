#ifndef __USART_H
#define __USART_H

#include "stm32f4xx.h"

void Usart1_Init(uint32_t baud);   /* USART1 PA9(TX)/PA10(RX), 8N1 */

#endif
