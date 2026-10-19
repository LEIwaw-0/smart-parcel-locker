/*
 * FreeRTOS V202212.01 —— 快递柜固件的工程侧配置
 *
 * 内核源码 vendored 在 ..\freertos\（include/ + tasks/list/queue + RVDS/ARM_CM4F + heap_4），
 * 本文件放在 app\ 下，随 .\app 一起被 IncludePath 命中；仓库自带内核，clone 即可编译。
 *
 * 基线参数与已验证过的 demo1 工程保持一致：
 *   tick 1ms、heap_4、max prio 10、Cortex-M4F 全抢占（NVIC 分组必须为 Group_4）。
 * 与 demo1 的差异：
 *   - 不需要软件定时器（定时全靠任务截止时间）-> configUSE_TIMERS = 0，
 *     因此工程里也不编译 timers.c
 *   - heap 从 20KB 提到 32KB（7 个任务栈 + 4 个队列都从这里分配）
 */
#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

/* 汇编器不需要 stdint；编译器（armcc/armclang/gcc）需要 */
#if defined( __ICCARM__ ) || defined( __CC_ARM ) || defined( __GNUC__ )
    #include <stdint.h>
    extern uint32_t SystemCoreClock;
#endif

#define configUSE_PREEMPTION                      1
#define configUSE_IDLE_HOOK                       0
#define configUSE_TICK_HOOK                       0
#define configCPU_CLOCK_HZ                        ( SystemCoreClock )   /* 168MHz */
#define configTICK_RATE_HZ                        ( ( TickType_t ) 1000 )
#define configMAX_PRIORITIES                      ( 10 )
#define configMINIMAL_STACK_SIZE                  ( ( unsigned short ) 128 )
#define configTOTAL_HEAP_SIZE                     ( ( size_t ) ( 32 * 1024 ) )
#define configMAX_TASK_NAME_LEN                   ( 16 )
#define configUSE_TRACE_FACILITY                  1
#define configUSE_16_BIT_TICKS                    0
#define configIDLE_SHOULD_YIELD                   1
#define configUSE_MUTEXES                         1
#define configQUEUE_REGISTRY_SIZE                 8

/* 栈溢出检测用方法 2：检查栈底 4 个字是否还是 0xA5A5A5A5 哨兵。
 * 方法 1 只在切换瞬间比较 SP，容易漏；方法 2 只要"曾经"写穿就一定能抓到。
 * 需要配套的 vApplicationStackOverflowHook（app/hooks.c）。 */
#define configCHECK_FOR_STACK_OVERFLOW           2

/* 开堆耗尽钩子：不开的话，heap_4 分配失败的表现是"某个任务悄悄不干活了"，
 * 既不是崩溃也不是报错，极难定位。 */
#define configUSE_MALLOC_FAILED_HOOK             1

#define configUSE_APPLICATION_TASK_TAG            0
#define configUSE_COUNTING_SEMAPHORES             1
#define configGENERATE_RUN_TIME_STATS             0

/* 本工程不使用软件定时器：所有周期行为都由任务的 xQueueReceive 超时 /
 * vTaskDelayUntil 承担，少一个内核任务、少一份 2KB 级栈开销。 */
#define configUSE_TIMERS                          0

/* 按需裁剪 API：只留本工程真正用到的 */
#define INCLUDE_vTaskPrioritySet                  1
#define INCLUDE_uxTaskPriorityGet                 1
#define INCLUDE_vTaskDelete                       1
#define INCLUDE_vTaskSuspend                      1
#define INCLUDE_vTaskDelayUntil                   1
#define INCLUDE_vTaskDelay                        1
#define INCLUDE_uxTaskGetStackHighWaterMark       1
#define INCLUDE_xTaskGetSchedulerState            1   /* delay_ms 判断"调度器是否已启动" */
#define INCLUDE_xTaskGetCurrentTaskHandle         1
#define INCLUDE_xTaskGetIdleTaskHandle            1

/* ---- Cortex-M 专有配置 ---- */
#ifdef __NVIC_PRIO_BITS
    #define configPRIO_BITS                       __NVIC_PRIO_BITS
#else
    #define configPRIO_BITS                       4
#endif

#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY          0xf

/* 中断里调用 FromISR 版本 API 的门槛（数值越小优先级越高，所以"5"意味着
 * 只有抢占优先级 5~15 的中断才允许碰内核 API）。本工程三个 USART 中断
 * 统一配成抢占优先级 6，见 main.c / usart.c / ld3320.c / esp8266.c。 */
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY     5

#define configKERNEL_INTERRUPT_PRIORITY          ( configLIBRARY_LOWEST_INTERRUPT_PRIORITY << ( 8 - configPRIO_BITS ) )
#define configMAX_SYSCALL_INTERRUPT_PRIORITY     ( configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << ( 8 - configPRIO_BITS ) )

/* 断言：不打哑谜。命中后由 app/hooks.c 用"不取锁、不依赖调度器"的裸串口
 * 打印 文件:行号 + 当前任务名 + 堆水位，然后立刻停机保住第一现场。
 * 这里只声明，因为此文件在包含 task.h 之前就要展开 configASSERT。 */
extern void vAssertCalled( const char *pcFile, unsigned long ulLine );
#define configASSERT( x )                                     \
    if( ( x ) == 0 )                                          \
    {                                                         \
        vAssertCalled( __FILE__, __LINE__ );                  \
    }

/* 把内核的 port 中断接到 CMSIS 标准向量名上（启动文件里是 [WEAK]，让位给它们） */
#define vPortSVCHandler     SVC_Handler
#define xPortPendSVHandler  PendSV_Handler
#define xPortSysTickHandler SysTick_Handler

#endif /* FREERTOS_CONFIG_H */
