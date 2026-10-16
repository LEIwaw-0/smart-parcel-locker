/**
  ******************************************************************************
  * @file    esp8266.h
  * @brief   ESP8266 WiFi模块 AT指令驱动头文�?(STM32F407�?
  *          串口: USART3 PB10(TX)/PB11(RX), 115200 8N1
  *
  *          将模块作为TCP客户端运行：
  *            AT -> AT+CWMODE=1 -> AT+CWJAP -> AT+CIPMUX=0
  *            -> AT+CIPSENDBUF=0 -> (10秒等�? -> AT+CIPSTART
  *
  *          ESP8266_SendText() 排队发送一行文本到中继服务器，
  *          ESP8266_GetResponse() 返回下一行服务器回复(AI回复)�?  *          均为非阻塞接口，状态机由主循环调用 ESP8266_Tick() 驱动�?  ******************************************************************************
  */
#ifndef __ESP8266_H
#define __ESP8266_H

#include "stm32f4xx.h"

/* 初始化USART3和状态机 */
void    ESP8266_Init(void);

/* 状态机推进，每次主循环调用一�?~5ms) */
void    ESP8266_Tick(void);

/* 1=Wifi已连+TCP已建立，可发送数�?*/
uint8_t ESP8266_IsReady(void);

/* 排队发送一行文�?最�?56字节)，返�?=�?不可�?*/
uint8_t ESP8266_SendText(const char *text);

/* 取下一行服务器回复，返�?=无数�?*/
uint8_t ESP8266_GetResponse(char *out, uint8_t maxlen);

/* 获取当前状态码(0..11，供UI显示) */
uint8_t ESP8266_GetStatus(void);

/* ======================== ���ʽ HTTP GET ========================
 * Temporarily closes the relay TCP link, opens a short-lived TCP connection
 * to <host>:80, issues one GET <path> HTTP/1.0, then hands the link back so
 * the main state machine rebuilds the relay connection.
 * All calls are non-blocking; the touch UI keeps running during a fetch. */
/* start a fetch: 0 = started, 1 = busy, 2 = rejected (bad args / link not up) */
uint8_t ESP8266_FetchStart(const char *host, const char *path);
/* poll from the main loop: 0 = running, 1 = ok (out = response), 2 = failed */
uint8_t ESP8266_FetchPoll(char *out, uint16_t maxlen);
/* 1 while a fetch owns the link */
uint8_t ESP8266_FetchBusy(void);

uint8_t ESP8266_SendIsPending(void);  /* 1 = 发送缓冲区忙, 0 = 空闲 */

/* ======================== WiFi/TCP配置 ========================
 * 修改以下参数以匹配你的网络和中继服务�? * ESP_TCP_IP = 运行 chat_relay.py 的电脑IP
 *              (Windows移动热点默认网关: 192.168.137.1) */
#define ESP_SSID   "ALLLFLYYDS"
#define ESP_PASS   "}64U4s77"
#define ESP_TCP_IP "192.168.137.1"
#define ESP_TCP_PORT 8090

#endif
