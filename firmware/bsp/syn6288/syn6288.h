/**
  ******************************************************************************
  * @file    syn6288.h
  * @brief   SYN6288 中文语音合成模块驱动 (STM32 侧)
  *
  * 适配板子: STM32F407VE
  * 串口:    UART4, PC10(TX) -> 模块 RXD, 9600 8N1
  * 方向:    仅发送 (TX-only), 不开 RX 中断, 避免接收处理引发 HardFault
  *
  * 帧格式 (文本合成, 命令码 0x01):
  *   [0xFD][0x00][len+3][0x01][0x01|Music<<4][文本...][ECC]
  *   len = 文本字节数; +3 = 命令字节 + 命令参数字节 + ECC 字节
  *   命令参数低4位 0x01 = GBK 编码 (与工程 GBK 中文字符串一致)
  *   高4位 = 背景音乐编号 (0=无, 1~15)
  *   ECC = 从 0xFD 到最后一个文本字节的逐字节异或 (无取反)
  *
  * 控制命令 (5 字节定长, ECC 已预算好, 见 syn6288.c):
  *   停止合成 / 暂停合成 / 恢复合成 / 状态查询 / 进入掉电
  *
  * 时序约束:
  *   - 模块上电后需等待 500ms 再发首条命令 (SYN6288_Init 内已处理)
  *   - 两条命令之间至少间隔 200ms (SYN_FrameInfo 发送后已延时 200ms)
  ******************************************************************************
  */
#ifndef __SYN6288_H
#define __SYN6288_H

#include "stm32f4xx.h"

/* 初始化 UART4 (PC10 TX), 9600 8N1, 仅发送; 内部延时 500ms 等待模块上电 */
void SYN6288_Init(void);

/* 发送一条文本合成帧
 * music: 背景音乐编号 0~15 (0=无背景音)
 * text : GBK 编码文本, 可含控制标签, 如 "[v8][m1][t5]你好"
 *        [v0~16] 音量, [m0~16] 背景音乐, [t0~5] 语速
 */
void SYN_FrameInfo(uint8_t music, const uint8_t *text);

/* 发送原始控制命令 (5 字节定长) */
void SYN_SendCmd(const uint8_t *cmd, uint8_t len);

/* 停止合成 */
void SYN_Stop(void);
/* 暂停合成 */
void SYN_Suspend(void);
/* 恢复合成 */
void SYN_Recover(void);
/* 查询模块状态 */
void SYN_QueryStatus(void);
/* 进入掉电模式 */
void SYN_PowerDown(void);

#endif
