#ifndef __HOOKS_H
#define __HOOKS_H

#include "stm32f4xx.h"

/******************************************************
**  hooks.h - the four "still able to speak" kernel exits
**
**  vAssertCalled()                  <- configASSERT
**  vApplicationStackOverflowHook()  <- configCHECK_FOR_STACK_OVERFLOW = 2
**  vApplicationMallocFailedHook()   <- configUSE_MALLOC_FAILED_HOOK = 1
**  HardFault_Handler()              <- startup file's [WEAK] vector
**
**  The first three are wired up by macros in FreeRTOSConfig.h, the fourth
**  by the weak symbol in startup_stm32f40_41xxx.s.  Nothing has to be
**  registered from main().
**
**  Three iron rules (a broken assert must not become a broken debugger):
**    1) only USART1 raw output here - never printf / never take a mutex
**    2) print the file:line, the task name and the heap water mark
**    3) on the way out: disable interrupts and spin forever, keep the scene
******************************************************/

/* 1 = vTaskStartScheduler() has been reached.  The hooks must check this
   before touching any kernel object: before the scheduler runs,
   pcTaskGetName(NULL) dereferences a NULL TCB and turns one fault into two. */
extern volatile uint8_t g_sched_started;

#endif /* __HOOKS_H */
