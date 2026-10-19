#ifndef __DELAY_H
#define __DELAY_H

#include "stm32f4xx.h"

/* ---- 延时 / 计时 ---- 
 * 裸机时代 delay_ms/delay_us 是直接操作 SysTick 寄存器实现的忙等。
 * 接入 FreeRTOS 后 SysTick 归内核所有（tick 中断），绝不能再被业务代码改写，
 * 所以这里的实现换成分工明确的两条路：
 *
 *   delay_us()  永远是硬等待 —— 用于 LCD/触摸/继电器这类"必须精确的短时序"，
 *               读者是纯硬件，不需要 CPU 让出；count 源换成 DWT 周期计数器
 *               （168MHz 下 1us = 168 周期），它不与任何内核资源冲突。
 *   delay_ms()  调度器启动前 = DWT 忙等（开机初始化那几次）；
 *               调度器启动后 = vTaskDelay，让出 CPU 给其它任务。
 *
 * 代价与约束：delay_us() 期间不会让出 CPU，所以只允许"微秒级"使用；
 * 毫秒级请一律调用 delay_ms()。
 * ------------------------------------------------------------- */
void Delay_Init(void);              /* 使能 DWT 周期计数器；上电第一件事 */
void delay_ms(uint32_t ms);         /* 调度器感知：启动后等价 vTaskDelay */
void delay_us(uint32_t us);         /* 纯硬等待，微秒级短时序专用 */

/* DWT 周期计数直读：给需要"非阻塞计时"的驱动用（lock.c 的 2s 断电保护等），
   返回值减去起始值再除以 168 就是 us 数，减法天然处理计数器回绕。 */
uint32_t Delay_CycNow(void);

#endif
