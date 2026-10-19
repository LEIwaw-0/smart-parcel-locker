/******************************************************
**  main.c - boot, peripheral bring-up, then hand over to FreeRTOS.
**
**  This file is all that is left of the original 2266-line monolith.
**  The application logic lives in the app_* modules beside it,
**  grouped by responsibility:
**
**      app_config.h  project-wide constants and layout macros
**      app_ctx.c/h   shared mutable state (was file-scope statics)
**      app_rtos.c/h  the event/IPC contract between the tasks
**      app_ui_task.c the UI task: event dispatch + deadline housekeeping
**      app_link.c/h  the ESP8266 link task (AT/TCP + uplink queue)
**      app_ui.c/h    title bar, cards, menu, overlays, status bar
**      app_clock.c/h wall clock + weather fetch over the ESP8266 link
**      app_door.c/h  relay drive and door-open feedback tracking
**      app_voice.c/h LD3320 voice front end + AI chat mode
**      app_cloud.c/h TCP link to the locker server (HELLO/STATUS/OPEN...)
**      app_flow.c/h  the deposit / pickup / admin state handlers
**
**  main() does only the bring-up that cannot be deferred: it needs
**  busy-wait timing and no other task running.  The scheduler is then
**  started from App_RtosStart() and main() never returns.
**
**      ui     <- touch/voice/cloud/door events; owns LCD, state, code table
**      touch  <- the panel, posts a sample every 5ms/finger
**      voice  <- the LD3320, posts every recognition result
**      door   <- the four relays + PCF8574 feedback, drives on q_door
**      link   <- the ESP8266, drains q_link, borrows the link for weather
**      clock  <- the wall clock + the weather cards
**      diag   <- heap/stack water marks every 5s (hooks.c for the rest)
**
**  Three things had to change for the kernel to co-exist with the drivers:
**      NVIC group    Group_2 -> Group_4 (all 4 bits preemption, so the
**                    numeric priority really is the preemption priority)
**      delays        Delay.c moved from SysTick to the DWT counter, since
**                    SysTick now fires the FreeRTOS tick
**      interrupts    USART1/2/3 preemption priority 6, i.e. at or below
**                    configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY
**
**  Copyright notice and hardware notes preserved from the original.
******************************************************/
#include "app_config.h"
#include "app_ctx.h"
#include "app_cloud.h"
#include "app_clock.h"
#include "app_door.h"
#include "app_flow.h"
#include "app_rtos.h"
#include "app_ui.h"
#include "app_voice.h"
#include "codebox.h"
#include "delay.h"
#include "esp8266.h"
#include "keypad.h"
#include "lcd.h"
#include "ld3320.h"
#include "lock.h"
#include "pcf8574.h"
#include "syn6288.h"
#include "touch.h"
#include "usart.h"
#include "hooks.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>
#include <string.h>

int main(void)
{
    uint8_t  i;
    uint8_t  pcf_addr;

    /* ---- 第一件事：把 DWT 周期计数器打开 ----
       它现在是 delay_us 和 lock.c 计时保护的唯一时基；SysTick 已经归
       FreeRTOS 所有，任何人都不许再写 SysTick->CTRL。 */
    Delay_Init();

    /* ---- boot serial + reset cause ---- */
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_4);  /* 4 bits preemption: the
        numeric priority IS the preemption priority, which is what the kernel
        and configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY assume */
    Usart1_Init(115200);
    delay_ms(100);
    printf("\r\n==========================================\r\n");
    printf("    Smart Locker (voice v2) / FreeRTOS\r\n");
    printf("==========================================\r\n");

    {
        uint32_t csr = RCC->CSR;
        printf("Reset CSR=0x%08lX:", csr);
        if (csr & RCC_CSR_PORRSTF)  printf(" POR");
        if (csr & RCC_CSR_BORRSTF)  printf(" BOR");
        if (csr & RCC_CSR_PADRSTF)  printf(" PAD");
        if (csr & RCC_CSR_SFTRSTF)  printf(" SFT");
        if (csr & RCC_CSR_WDGRSTF)  printf(" WDG");
        if (csr & RCC_CSR_WWDGRSTF) printf(" WWDG");
        if (csr & RCC_CSR_LPWRRSTF) printf(" LPWR");
        printf("\r\n");
        RCC->CSR |= RCC_CSR_RMVF;
    }

    /* ---- PCF8574 scan ---- */
    printf("BOOT: init I2C1 (PB6/PB7), scan PCF8574...\r\n");
    pcf_addr = PCF8574_Init();
    {
        uint8_t alist[16];
        uint8_t an = PCF8574_ScanBus(alist, 16);
        uint8_t k;
        if (an == 0)
            printf("SCAN: no I2C device at 0x01..0x7E\r\n");
        else
        {
            printf("SCAN: %d device(s):", an);
            for (k = 0; k < an; k++) printf(" 0x%02X", alist[k]);
            printf("\r\n");
        }
    }
    if (pcf_addr == 0)
        printf("BOOT: !! no PCF8574 detected - P4-P7 feedback unavailable\r\n");
    else
        printf("BOOT: PCF8574 at 0x%02X (P4-P7 = feedback input)\r\n", pcf_addr);

    Lock_Init();

    /* ---- 上电安全: 只强制关锁, 绝不做开锁自检 ----
       原自检会让4路继电器各吸合300ms, 等于每次上电都把4个柜门打开一次,
       存在安全隐患。现在上电只把所有线圈置为断开, 再读一次P4-P7门状态反馈,
       这样继电器粘连仍能在串口看到, 但不会打开任何柜门。 */
    printf("BOOT: forcing all locks OFF (no self-test pulse)\r\n");
    for (i = 1; i <= LOCK_COUNT; i++)
        Lock_Close(i);
    delay_ms(100);
    for (i = 1; i <= LOCK_COUNT; i++)
    {
        uint8_t st = Lock_ReadFeedback(i);
        printf("BOOT: lock %d -> %s\r\n", (int)i,
               (st == 1) ? "OPEN" : (st == 0) ? "closed" : "read-fail");
    }

    /* ---- LCD + touch ---- */
    Touch_Init();
    LCD_Init();
    LCD_Backlight(1);

    /* ---- code table (Flash Sector 11) ---- */
    CodeBox_Init();
    printf("BOOT: code table loaded, %d/%d boxes in use\r\n",
           CodeBox_CountUsed(), BOX_COUNT);

    Menu_Draw();
    printf("BOOT: UI ready (menu)\r\n");

    /* ---- SYN6288 + LD3320 ---- */
    SYN6288_Init();
    LD3320_Init();
    ESP8266_Init();
    printf("BOOT: SYN6288 (PC10) + LD3320 (PA3) + ESP8266 (PB10) initialised\r\n");

    SYN_FrameInfo(0, (uint8_t *)"[v8][t5][m0]\xBB\xB6\xD3\xAD\xCA\xB9\xD3\xC3\xBF\xEC\xB5\xDD\xB9\xF1");
    printf("BOOT: welcome spoken\r\n");
    delay_ms(1500);

    printf("BOOT: tap DEPOSIT or PICKUP, or say 'xiao ying' + 6 digits.\r\n");

    /* ---- 交接：建 IPC + 建任务 + 启动调度器，main() 从此不再返回 ----
       所有寄存器级的初始化都已经做完，此刻建队列/任务是单线程的，
       不存在任何竞争。 */
    printf("BOOT: starting FreeRTOS scheduler (tick %d Hz)...\r\n\r\n",
           (int)configTICK_RATE_HZ);
    App_RtosStart();

    /* App_RtosStart() 只在堆耗尽时返回 */
    printf("BOOT: !! vTaskStartScheduler returned - heap exhausted?\r\n");
    for (;;) { }
}
