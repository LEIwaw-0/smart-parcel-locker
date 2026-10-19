/******************************************************
**  Delay.c - DWT based delays, scheduler aware
**
**  The bare-metal version drove SysTick->CTRL/LOAD/VAL directly, which
**  cannot survive alongside FreeRTOS: SysTick now belongs to the kernel
**  and fires the tick interrupt.  Both functions are re-implemented on
**  the DWT cycle counter instead:
**
**      delay_us()  168 cycles per us at 168MHz, always a hard wait
**      delay_ms()  vTaskDelay() once the scheduler runs, DWT busy wait
**                  before that (boot bring-up)
**
**  The DWT counter is a plain free-running 32-bit register, so the
**  (now - start) subtraction handles wrap-around by itself; the only
**  real limit is the 25.5s wrap at 168MHz, which no caller comes close to.
******************************************************/
#include "Delay.h"
#include "FreeRTOS.h"
#include "task.h"

#define DELAY_CYC_PER_US   ( SystemCoreClock / 1000000u )   /* 168 @168MHz */
#define DELAY_CYC_PER_MS   ( SystemCoreClock / 1000u )      /* 168000      */

void Delay_Init(void)
{
    /* DWT 周期计数器要先打开调试跟踪单元的电源（TRCENA），再单独使能计数。
       这一步在 lock.c 里也做过一次，重复调用无副作用。 */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL  |= DWT_CTRL_CYCCNTENA_Msk;
}

uint32_t Delay_CycNow(void)
{
    return DWT->CYCCNT;
}

void delay_us(uint32_t us)
{
    uint32_t start, elapsed, want;

    if (us == 0u) return;

    /* 短等待也要防一手"CYCCNT 没使能"：那样会直接死在这里，
       而不是让 LCD 花屏。宁可延时不准也不要卡死。 */
    if ((DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) == 0u) return;

    want  = us * DELAY_CYC_PER_US;
    start = DWT->CYCCNT;
    do
    {
        elapsed = DWT->CYCCNT - start;   /* 无符号减法：回绕自动正确 */
    } while (elapsed < want);
}

void delay_ms(uint32_t ms)
{
    if (ms == 0u) return;

    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
    {
        /* 任务上下文：挂起本任务，把 CPU 交给别人（这正是接入 RTOS 的意义） */
        vTaskDelay(pdMS_TO_TICKS(ms));
        return;
    }

    /* 调度器还没跑（上电初始化）：只能忙等。用 DWT 而不是 SysTick。 */
    if ((DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) != 0u)
    {
        uint32_t start = DWT->CYCCNT;
        uint32_t want  = ms * DELAY_CYC_PER_MS;
        while ((DWT->CYCCNT - start) < want) { }
    }
}
