/******************************************************
**  hooks.c - assert / stack overflow / heap exhaustion / hard fault
**
**  Why a file of its own: these four entries are called from inside the
**  kernel, at the exact moment when nothing else can be trusted.  So they
**  use only the USART1 raw layer (read a flag, write DR, loop) - no
**  printf, no mutex, no heap, no kernel object.  Everything they print
**  goes straight out of the UART, even if the scheduler never started.
**
**  vAssertCalled                 <- configASSERT (FreeRTOSConfig.h)
**  vApplicationStackOverflowHook <- configCHECK_FOR_STACK_OVERFLOW = 2
**  vApplicationMallocFailedHook  <- configUSE_MALLOC_FAILED_HOOK = 1
**  HardFault_Handler             <- startup file [WEAK] vector
******************************************************/
#include "hooks.h"
#include "usart.h"
#include "FreeRTOS.h"
#include "task.h"

/* 由 main() 在 vTaskStartScheduler() 之前置 1 */
volatile uint8_t g_sched_started = 0;

static volatile uint8_t s_in_hook = 0;      /* 防重入：钩子里再触发钩子 */

/*------------------------------------------------------
 * 小工具
 *------------------------------------------------------*/
static void prvEnterHook(void)
{
    if (s_in_hook != 0)
    {
        /* 打印本身又把钩子触发了：再打只会把串口刷爆，直接停机 */
        for (;;) { }
    }
    s_in_hook = 1;
    portDISABLE_INTERRUPTS();       /* 冻住现场，不让别的任务继续改内存 */
}

static void prvPrintTaskName(void)
{
    if (g_sched_started == 0)
    {
        Usart1_RawPuts("(scheduler not started)");
        return;
    }
    Usart1_RawPuts(pcTaskGetName(NULL));    /* NULL = 当前任务 */
}

static void prvPrintHeapStatus(void)
{
    Usart1_RawPuts("  free heap now : ");
    Usart1_RawPutDec((uint32_t)xPortGetFreeHeapSize());
    Usart1_RawPuts(" bytes (of ");
    Usart1_RawPutDec((uint32_t)configTOTAL_HEAP_SIZE);
    Usart1_RawPuts(")\n");

    Usart1_RawPuts("  min ever free : ");
    Usart1_RawPutDec((uint32_t)xPortGetMinimumEverFreeHeapSize());
    Usart1_RawPuts(" bytes   <- lowest water mark ever\n");
}

/*------------------------------------------------------
 * 一、断言：configASSERT(x) 展开后就是调用它
 *   它能在临界区里、甚至在调度器启动前被安全调用，因为只用裸串口 + 死循环。
 *------------------------------------------------------*/
void vAssertCalled(const char *pcFile, unsigned long ulLine)
{
    prvEnterHook();

    Usart1_RawPuts("\n");
    Usart1_RawPuts("======== ASSERT FAILED ========\n");
    Usart1_RawPuts("  file : ");
    Usart1_RawPuts((pcFile != NULL) ? pcFile : "(null)");
    Usart1_RawPuts("\n  line : ");
    Usart1_RawPutDec((uint32_t)ulLine);
    Usart1_RawPuts("\n  task : ");
    prvPrintTaskName();
    Usart1_RawPuts("\n");
    prvPrintHeapStatus();
    Usart1_RawPuts("  next : open that file, jump to that line, read the condition\n");
    Usart1_RawPuts("===============================\n");

    for (;;) { }
}

/*------------------------------------------------------
 * 二、栈溢出
 *   时机：不是溢出那一刻，而是任务被切换出去时由 vTaskSwitchContext()
 *         检查栈底 4 个字是否还是 0xA5A5A5A5（configCHECK_FOR_STACK_OVERFLOW = 2）。
 *         所以必须真的发生过一次任务切换，钩子才会响。
 *------------------------------------------------------*/
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask;

    prvEnterHook();

    Usart1_RawPuts("\n");
    Usart1_RawPuts("===== STACK OVERFLOW =====\n");
    Usart1_RawPuts("  task : ");
    Usart1_RawPuts((pcTaskName != NULL) ? pcTaskName : "(null)");
    Usart1_RawPuts("\n");
    Usart1_RawPuts("  why  : the 4 guard words at the BOTTOM of this task stack\n");
    Usart1_RawPuts("         are no longer 0xA5A5A5A5\n");
    Usart1_RawPuts("  next : raise that task's stack in app_rtos.c (xTaskCreate depth)\n");
    Usart1_RawPuts("==========================\n");

    for (;;) { }
}

/*------------------------------------------------------
 * 三、堆耗尽
 *   位置：heap_4.c 的 pvPortMalloc() 末尾，且在 xTaskResumeAll() 之后回调，
 *         所以此刻调度器是正常运行的，能放心打印任务名。
 *------------------------------------------------------*/
void vApplicationMallocFailedHook(void)
{
    prvEnterHook();

    Usart1_RawPuts("\n");
    Usart1_RawPuts("===== MALLOC FAILED =====\n");
    Usart1_RawPuts("  task : ");
    prvPrintTaskName();
    Usart1_RawPuts("\n");
    prvPrintHeapStatus();
    Usart1_RawPuts("  why  : heap_4 found no free block big enough for the request\n");
    Usart1_RawPuts("  next : configTOTAL_HEAP_SIZE / a queue or stack is oversized\n");
    Usart1_RawPuts("=========================\n");

    for (;;) { }
}

/*------------------------------------------------------
 * 四、硬故障：把出错那一刻的 PC / LR 从异常栈帧里挖出来
 *
 * 为什么这段用 __asm 函数而不是普通 C 函数：
 *   异常进入时 CPU 已把 8 个字压到"出错时正在用的那个栈"上
 *       [低] R0 R1 R2 R3 R12 LR PC xPSR [高]
 *   要读到它必须先知道当时用的是 MSP 还是 PSP，也就是 EXC_RETURN
 *   （异常进入时被搬进 LR）的 bit2。普通 C 函数的序言会挪动 SP，
 *   此时 MRSEQ R0,MSP 拿到的已经不是异常栈帧首地址；__asm 函数不生成序言，
 *   进去时寄存器还是"原装"的。
 *   bit2: 0 = MSP（中断里出错）  1 = PSP（任务里出错）
 *
 * !! armcc 的嵌入式汇编器按本地代码页解释源文件，块内注释必须是纯 ASCII，
 *    中文说明只能写在这段 C 注释里，否则报 "error #7: unrecognized token"。!!
 *------------------------------------------------------*/
__asm void HardFault_Handler(void)
{
    IMPORT  vHardFaultReport
    TST     LR, #4              ; bit2: 0 = MSP (fault in ISR), 1 = PSP (in task)
    ITE     EQ
    MRSEQ   R0, MSP
    MRSNE   R0, PSP
    MOV     R1, LR
    B       vHardFaultReport    ; tail call, never returns
}

void vHardFaultReport(uint32_t *pulFrame, uint32_t ulExcReturn)
{
    prvEnterHook();

    Usart1_RawPuts("\n");
    Usart1_RawPuts("========== HARD FAULT ==========\n");
    Usart1_RawPuts("  from : ");
    Usart1_RawPuts(((ulExcReturn & 0x4u) != 0u) ? "task (PSP)" : "handler/ISR (MSP)");
    Usart1_RawPuts("\n  task : ");
    prvPrintTaskName();
    Usart1_RawPuts("\n");

    /* 异常栈帧固定 8 个字且就在 SP 处；即使开了 FPU 多压了 s0-s15+FPSCR，
       那部分压在更低的地址，pulFrame[0..7] 与 R0..xPSR 的对应关系不变。 */
    Usart1_RawPuts("  --- exception stack frame ---\n");
    Usart1_RawPuts("  R0   = "); Usart1_RawPutHex32(pulFrame[0]); Usart1_RawPuts("\n");
    Usart1_RawPuts("  R1   = "); Usart1_RawPutHex32(pulFrame[1]); Usart1_RawPuts("\n");
    Usart1_RawPuts("  R2   = "); Usart1_RawPutHex32(pulFrame[2]); Usart1_RawPuts("\n");
    Usart1_RawPuts("  R3   = "); Usart1_RawPutHex32(pulFrame[3]); Usart1_RawPuts("\n");
    Usart1_RawPuts("  R12  = "); Usart1_RawPutHex32(pulFrame[4]); Usart1_RawPuts("\n");
    Usart1_RawPuts("  LR   = "); Usart1_RawPutHex32(pulFrame[5]); Usart1_RawPuts("\n");
    Usart1_RawPuts("  PC   = "); Usart1_RawPutHex32(pulFrame[6]);
    Usart1_RawPuts("   <== the instruction that blew up\n");
    Usart1_RawPuts("  xPSR = "); Usart1_RawPutHex32(pulFrame[7]); Usart1_RawPuts("\n");

    Usart1_RawPuts("  --- fault status ---\n");
    Usart1_RawPuts("  CFSR  = "); Usart1_RawPutHex32(SCB->CFSR);  Usart1_RawPuts("\n");
    Usart1_RawPuts("  HFSR  = "); Usart1_RawPutHex32(SCB->HFSR);  Usart1_RawPuts("\n");
    Usart1_RawPuts("  MMFAR = "); Usart1_RawPutHex32(SCB->MMFAR); Usart1_RawPuts("\n");
    Usart1_RawPuts("  BFAR  = "); Usart1_RawPutHex32(SCB->BFAR);  Usart1_RawPuts("\n");
    Usart1_RawPuts("================================\n");

    for (;;) { }
}
