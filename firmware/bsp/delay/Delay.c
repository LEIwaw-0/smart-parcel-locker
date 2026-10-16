#include "delay.h"
//void delay_ms(uint32_t ms)
//{
//	while (ms--)
//	{
//		SysTick->CTRL = 0; 			// 关闭系统定时器后才能配置寄存器
//		SysTick->LOAD = 21000 - 1; 			// 设置计数值，用于设置定时的时间
//		SysTick->VAL = 0; 			// 清空当前值还有计数标志位
//		SysTick->CTRL = 1; 			// 使能系统定时器工作，且时钟源为系统时钟的8分频（168MHz/8=21MHz）
//		while ((SysTick->CTRL & (1 << 16)) == 0);	// 等待系统定时器计数完毕
//		SysTick->CTRL = 0; 			// 关闭系统定时器	
//	}
//}
void delay_ms(uint32_t ms)
{
	while (ms--)
	{
		SysTick->CTRL = 0; 			// 关闭系统定时器后才能配置寄存器
		SysTick->LOAD = 168000 - 1; 			// 设置计数值，用于设置定时的时间
		SysTick->VAL = 0; 			// 清空当前值还有计数标志位
		SysTick->CTRL = 5; 			// 使能系统定时器工作，且时钟源为系统时钟的8分频（168MHz/8=21MHz）
		while ((SysTick->CTRL & (1 << 16)) == 0);	// 等待系统定时器计数完毕
		SysTick->CTRL = 0; 			// 关闭系统定时器	
	}
}

//void delay_us(uint32_t us)
//{
//	while (us--)
//	{
//		SysTick->CTRL = 0; 			// 关闭系统定时器后才能配置寄存器
//		SysTick->LOAD = 21 - 1; 			// 设置计数值，用于设置定时的时间
//		SysTick->VAL = 0; 			// 清空当前值还有计数标志位
//		SysTick->CTRL = 1; 			// 使能系统定时器工作，且时钟源为系统时钟的8分频（168MHz/8=21MHz）
//		while ((SysTick->CTRL & (1 << 16)) == 0);	// 等待系统定时器计数完毕
//		SysTick->CTRL = 0; 			// 关闭系统定时器	
//	}
//}

void delay_us(uint32_t us)
{
	while (us--)
	{
		SysTick->CTRL = 0; 			// 关闭系统定时器后才能配置寄存器
		SysTick->LOAD = 168 - 1; 			// 设置计数值，用于设置定时的时间
		SysTick->VAL = 0; 			// 清空当前值还有计数标志位
		SysTick->CTRL = 5; 			// 使能系统定时器工作，且时钟源为系统时钟的8分频（168MHz/8=21MHz）
		while ((SysTick->CTRL & (1 << 16)) == 0);	// 等待系统定时器计数完毕
		SysTick->CTRL = 0; 			// 关闭系统定时器	
	}
}
