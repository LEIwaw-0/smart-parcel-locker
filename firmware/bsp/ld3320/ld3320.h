/**
  ******************************************************************************
  * @file    ld3320.h
  * @brief   UNV-LD3320+STC11 module UART driver (STM32 side)
  *          USART2 PA2(TX)/PA3(RX), 9600 8N1, line parser.
  *
  *          Three line protocols from STC11 firmware:
 *            "CMD:XX"        legacy command (XX hex: 00, FE)
 *            "PCODE:DDDDDD"  pending code, awaiting dui/cuo confirmation
 *            "CODE:DDDDDD"   confirmed 6-digit pickup code
 *            "RETRY"         user said "cuo": re-speak the 6 digits
  ******************************************************************************
  */
#ifndef __LD3320_H
#define __LD3320_H

#include "stm32f4xx.h"

void LD3320_Init(void);
int  LD3320_GetCmd(void);   /* -1 = none, else last recognized code */
uint8_t LD3320_GetCode(char out_code[7]);       /* confirmed code */
uint8_t LD3320_GetPendingCode(char out_code[7]);/* pending code (readback) */
uint8_t LD3320_GetRetry(void);                  /* 1 = user said cuo at confirm */
uint8_t LD3320_GetDigit(uint8_t *idx, uint8_t *val); /* per-digit progress */
uint8_t LD3320_GetUndo(void);                   /* 1 = cuo during capture */
uint8_t LD3320_GetChat(uint8_t *code);         /* chat keyword code (0x40+)  */
uint8_t LD3320_GetChatWake(void);               /* 1 = "xiao zhi" chat wake   */
uint8_t LD3320_GetRaw(char *out, uint8_t maxlen); /* last raw UART line (debug) */

/* command codes emitted by STC11 firmware (legacy CMD:XX protocol) */
#define LD_CMD_PASSWORD    0x00   /* "xiao ying" password heard   */
#define LD_CMD_DOOR_OPEN   0x01   /* kai men                      */
#define LD_CMD_DOOR_CLOSE  0x02   /* guan men                     */
#define LD_CMD_PICKUP      0x03   /* qu jian                      */
#define LD_CMD_DEPOSIT     0x04   /* tou jian                     */
#define LD_CMD_RESET       0x05   /* chong shi                    */
#define LD_CMD_NO_PASSWD   0xFE   /* command without password     */
#define LD_CMD_ERROR       0xFF   /* recognition error            */

#endif
