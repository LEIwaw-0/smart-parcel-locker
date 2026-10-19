/**
  ******************************************************************************
  * @file    usart.c  (shared library, restored)
  * @brief   USART1 debug console for STM32F407: PA9(TX)/PA10(RX), 8N1
  *          - printf retarget via fputc
  *          - RXNE interrupt: received data is DISCARDED (no echo,
  *            keeps serial terminal clean)
  *          - raw output layer for the FreeRTOS fault hooks (see usart.h)
  ******************************************************************************
  */
#include "usart.h"
#include "FreeRTOS.h"      /* 只为 fputc 的日志互斥量 */
#include "semphr.h"
#include "task.h"          /* xTaskGetSchedulerState */

static SemaphoreHandle_t s_log_mtx   = NULL;   /* printf 串行化 */
static uint8_t           s_uart_ready = 0;     /* raw 输出层就绪标志 */

/* ===================== printf retarget =====================
 * 多任务下 printf 会被字符级打断（A 任务打出半个数字，B 任务插进来），
 * 所以这里按"字符"粒度取日志互斥量：占用极短（115200 下一个字符 87us），
 * 谁也不会因为打印饿死别人。取锁失败（超时）时仍然照打，宁可日志交错，
 * 也不能把日志丢掉。
 * 调度器启动前（开机那一串 BOOT 打印）不取锁：此时只有一条执行流。
 * ============================================================ */
int fputc(int ch, FILE *f)
{
    if ((s_log_mtx != NULL) && (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED))
    {
        if (xSemaphoreTake(s_log_mtx, pdMS_TO_TICKS(50)) == pdTRUE)
        {
            while (USART_GetFlagStatus(USART1, USART_FLAG_TC) == RESET)
                ;
            USART_SendData(USART1, (uint8_t)ch);
            xSemaphoreGive(s_log_mtx);
            return ch;
        }
    }

    while (USART_GetFlagStatus(USART1, USART_FLAG_TC) == RESET)
        ;
    USART_SendData(USART1, (uint8_t)ch);
    return ch;
}

/* ===================== USART1 init ===================== */
void Usart1_Init(uint32_t baud)
{
    GPIO_InitTypeDef  GPIO_InitStructure;
    USART_InitTypeDef USART_InitStructure;
    NVIC_InitTypeDef  NVIC_InitStructure;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1, ENABLE);
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);

    /* PA9(TX)/PA10(RX): AF, push-pull */
    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_9 | GPIO_Pin_10;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF;
    GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_100MHz;
    GPIO_InitStructure.GPIO_PuPd  = GPIO_PuPd_NOPULL;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    GPIO_PinAFConfig(GPIOA, GPIO_PinSource9,  GPIO_AF_USART1);
    GPIO_PinAFConfig(GPIOA, GPIO_PinSource10, GPIO_AF_USART1);

    /* 8N1, duplex */
    USART_InitStructure.USART_BaudRate           = baud;
    USART_InitStructure.USART_WordLength         = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits           = USART_StopBits_1;
    USART_InitStructure.USART_Parity             = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode               = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(USART1, &USART_InitStructure);

    /* RXNE interrupt: ISR drains DR and discards (no echo noise) */
    USART_ITConfig(USART1, USART_IT_RXNE, ENABLE);

    /* FreeRTOS 接入后 NVIC 分组必须是 Group_4（4 位全抢占），中断优先级
       数值即抢占优先级；低于 configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY=5
       的中断不允许调用 FromISR 版本的内核 API。这里统一用 6。 */
    NVIC_InitStructure.NVIC_IRQChannel                   = USART1_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 6;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority        = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    USART_Cmd(USART1, ENABLE);

    /* 日志互斥量：在调度器启动前建好，任务跑起来后 fputc 才用它 */
    if (s_log_mtx == NULL)
        s_log_mtx = xSemaphoreCreateMutex();

    s_uart_ready = 1;       /* ★ 放最后一行：此前任何 raw 输出都安全丢弃 */
}

/* ===================== USART1 IRQ: discard RX ===================== */
void USART1_IRQHandler(void)
{
    if (USART_GetITStatus(USART1, USART_IT_RXNE) != RESET)
    {
        (void)USART_ReceiveData(USART1);    /* read DR clears RXNE, discard */
    }
    if (USART_GetITStatus(USART1, USART_IT_ORE) != RESET)
    {
        (void)USART1->SR;
        (void)USART1->DR;
    }
}

/* ===================== 裸输出层（给 hooks.c 用） =====================
 * 铁律：只允许出现"读标志位 + 写数据寄存器 + 循环"，绝不取锁、绝不调
 * 内核 API、绝不用堆。一旦这里变得"聪明"，整套故障诊断就废了。
 * ================================================================== */
uint8_t Usart1_IsReady(void)
{
    return s_uart_ready;
}

void Usart1_RawPutc(char c)
{
    if (s_uart_ready == 0) return;          /* 未初始化：丢弃，绝不死等一个没配好的外设 */
    while (USART_GetFlagStatus(USART1, USART_FLAG_TC) == RESET)
        ;
    USART_SendData(USART1, (uint8_t)c);
}

void Usart1_RawPuts(const char *s)
{
    char prev = '\0';
    if (s == NULL) return;
    if (s_uart_ready == 0) return;

    while (*s != '\0')
    {
        if ((*s == '\n') && (prev != '\r')) Usart1_RawPutc('\r');   /* 串口助手要 CRLF */
        Usart1_RawPutc(*s);
        prev = *s;
        s++;
    }
}

void Usart1_RawPutHex32(uint32_t v)
{
    static const char hexdig[] = "0123456789ABCDEF";
    int i;
    if (s_uart_ready == 0) return;

    Usart1_RawPutc('0');
    Usart1_RawPutc('x');
    for (i = 28; i >= 0; i -= 4) Usart1_RawPutc(hexdig[(v >> i) & 0x0Fu]);
}

void Usart1_RawPutDec(uint32_t v)
{
    char buf[10];
    int  n = 0;
    if (s_uart_ready == 0) return;

    if (v == 0u) { Usart1_RawPutc('0'); return; }
    while ((v > 0u) && (n < (int)sizeof(buf)))
    {
        buf[n] = (char)('0' + (v % 10u));
        n++;
        v /= 10u;
    }
    while (n > 0) { n--; Usart1_RawPutc(buf[n]); }
}
