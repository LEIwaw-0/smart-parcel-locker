
#ifndef __ESP8266_H
#define __ESP8266_H

#include "stm32f4xx.h"

/* 初始化USART3和状态机 */
void    ESP8266_Init(void);

void    ESP8266_Tick(void);

uint8_t ESP8266_IsReady(void);

uint8_t ESP8266_SendText(const char *text);

uint8_t ESP8266_GetResponse(char *out, uint8_t maxlen);

/* 获取当前状态码(0..11，供UI显示) */
uint8_t ESP8266_GetStatus(void);

/* start a fetch: 0 = started, 1 = busy, 2 = rejected (bad args / link not up) */
uint8_t ESP8266_FetchStart(const char *host, const char *path);
/* poll from the main loop: 0 = running, 1 = ok (out = response), 2 = failed */
uint8_t ESP8266_FetchPoll(char *out, uint16_t maxlen);
/* 1 while a fetch owns the link */
uint8_t ESP8266_FetchBusy(void);

uint8_t ESP8266_SendIsPending(void);  /* 1 = 发送缓冲区忙, 0 = 空闲 */

#define ESP_SSID   "ALLLFLYYDS"
#define ESP_PASS   "}64U4s77"
#define ESP_TCP_IP "192.168.137.1"
#define ESP_TCP_PORT 8090

#endif
