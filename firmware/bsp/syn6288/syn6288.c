/**
  ******************************************************************************
  * @file    syn6288.c
  * @brief   SYN6288 语音合成模块驱动 (STM32F407VE 侧, 串口版)
  *
  * 接线: STM32 PC10(UART4_TX) -> 模块 RXD
  *       模块 VCC->5V, GND->GND (与 STM32 共地)
  *       模块 TXD 可不接 (本驱动为 TX-only)
  * 协议参考: SYN6288 数据手册 + 厂家 STM32F405 例程
  ******************************************************************************
  */
#include "syn6288.h"
#include "delay.h"
#include <string.h>

/* ===================== 控制命令 (5字节定长, ECC 已预算) ===================== */
/* ECC = 0xFD ^ 0x00 ^ 0x02 ^ 命令码, 无取反 */
static const uint8_t SYN_StopCmd[]      = {0xFD, 0x00, 0x02, 0x02, 0xFD}; /* 停止合成 */
static const uint8_t SYN_SuspendCmd[]   = {0xFD, 0x00, 0x02, 0x03, 0xFC}; /* 暂停合成 */
static const uint8_t SYN_RecoverCmd[]   = {0xFD, 0x00, 0x02, 0x04, 0xFB}; /* 恢复合成 */
static const uint8_t SYN_StatusCmd[]    = {0xFD, 0x00, 0x02, 0x21, 0xDE}; /* 状态查询 */
static const uint8_t SYN_PowerDownCmd[] = {0xFD, 0x00, 0x02, 0x88, 0x77}; /* 进入掉电 */

#define SYN_MAX_TEXT  96   /* 单帧文本最大字节数 (含标签), 避免帧缓冲溢出 */

/* ===================== UART4 初始化: PC10 TX, 9600, 仅发送 ===================== */
void SYN6288_Init(void)
{
    GPIO_InitTypeDef  GPIO_InitStructure;
    USART_InitTypeDef USART_InitStructure;

    RCC_APB1PeriphClockCmd(RCC_APB1Periph_UART4, ENABLE);
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOC, ENABLE);

    /* 仅配置 PB10(TX), 不配置 PC11(RX), TX-only */
    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_10;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF;
    GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_PuPd  = GPIO_PuPd_UP;
    GPIO_Init(GPIOC, &GPIO_InitStructure);

    GPIO_PinAFConfig(GPIOC, GPIO_PinSource10, GPIO_AF_UART4);

    USART_InitStructure.USART_BaudRate            = 9600;
    USART_InitStructure.USART_WordLength          = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits            = USART_StopBits_1;
    USART_InitStructure.USART_Parity              = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode                = USART_Mode_Tx;   /* 仅发送 */
    USART_Init(UART4, &USART_InitStructure);

    USART_Cmd(UART4, ENABLE);

    /* 模块上电稳定: 首条命令前至少等 500ms */
    delay_ms(500);
}

/* ===================== 底层发送 ===================== */
static void SYN_SendBytes(const uint8_t *buf, uint16_t len)
{
    uint16_t i;
    for (i = 0; i < len; i++)
    {
        while (USART_GetFlagStatus(UART4, USART_FLAG_TXE) == RESET)
            ;
        USART_SendData(UART4, buf[i]);
    }
    while (USART_GetFlagStatus(UART4, USART_FLAG_TC) == RESET)
        ;
}

/* ===================== 文本合成帧 =====================
 * 帧: [0xFD][0x00][len+3][0x01][0x01|Music<<4][文本...][ECC]
 * ECC = 0xFD ^ 0x00 ^ (len+3) ^ 0x01 ^ (0x01|Music<<4) ^ 每个文本字节
 */
void SYN_FrameInfo(uint8_t music, const uint8_t *text)
{
    uint8_t  frame[SYN_MAX_TEXT + 6];
    uint16_t text_len;
    uint8_t  ecc = 0;
    uint16_t i;

    text_len = strlen((const char *)text);
    if (text_len > SYN_MAX_TEXT)
        text_len = SYN_MAX_TEXT;

    frame[0] = 0xFD;                       /* 帧头 */
    frame[1] = 0x00;                       /* 数据区长度高字节 */
    frame[2] = (uint8_t)(text_len + 3);    /* 数据区长度低字节 */
    frame[3] = 0x01;                       /* 命令: 合成并播放 */
    frame[4] = (uint8_t)(0x01 | (music << 4)); /* 参数: GBK编码 + 背景音乐 */

    memcpy(&frame[5], text, text_len);     /* 文本内容 */

    for (i = 0; i < 5 + text_len; i++)     /* 异或校验: 从帧头到文本末尾 */
        ecc ^= frame[i];
    frame[5 + text_len] = ecc;             /* 校验字节 (无取反) */

    SYN_SendBytes(frame, 5 + text_len + 1);

    delay_ms(200);                         /* 命令间最小间隔 */
}

/* ===================== 控制命令 ===================== */
void SYN_SendCmd(const uint8_t *cmd, uint8_t len)
{
    SYN_SendBytes(cmd, len);
    delay_ms(200);
}

void SYN_Stop(void)        { SYN_SendCmd(SYN_StopCmd,      sizeof(SYN_StopCmd)); }
void SYN_Suspend(void)     { SYN_SendCmd(SYN_SuspendCmd,   sizeof(SYN_SuspendCmd)); }
void SYN_Recover(void)     { SYN_SendCmd(SYN_RecoverCmd,   sizeof(SYN_RecoverCmd)); }
void SYN_QueryStatus(void) { SYN_SendCmd(SYN_StatusCmd,    sizeof(SYN_StatusCmd)); }
void SYN_PowerDown(void)   { SYN_SendCmd(SYN_PowerDownCmd, sizeof(SYN_PowerDownCmd)); }
