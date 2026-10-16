/**
  ******************************************************************************
  * @file    usart.c  (shared library, restored)
  * @brief   USART1 debug console for STM32F407: PA9(TX)/PA10(RX), 8N1
  *          - printf retarget via fputc (MicroLIB)
  *          - RXNE interrupt: received data is DISCARDED (no echo,
  *            keeps serial terminal clean)
  ******************************************************************************
  */
#include "usart.h"
#include <stdio.h>

/* ===================== printf retarget (MicroLIB) ===================== */
int fputc(int ch, FILE *f)
{
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

    NVIC_InitStructure.NVIC_IRQChannel                   = USART1_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority        = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    USART_Cmd(USART1, ENABLE);
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
