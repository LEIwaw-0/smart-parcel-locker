#ifndef __APP_VOICE_H
#define __APP_VOICE_H

/******************************************************
**  app_voice.h - LD3320 voice front end + AI chat mode.
**
**  The STC11 sub-MCU on the LD3320 board emits short ASCII lines over
**  USART2.  Voice_Task() polls the driver's one-shot flags and republishes
**  each one as an EV_VOICE message on q_ui; the UI task decides what it
**  means for the current screen (keypad digit, pickup code, wake word...).
**
**  Voice_HandleCode() is the single place where a recognised 6-digit
**  code reaches the door, so the 8s de-dup guard lives here.
******************************************************/
#include "app_config.h"

void Voice_Task(void *arg);

void Voice_HandleCode(const char *code6);
void Voice_HandleDigit(uint8_t idx, uint8_t val);
void Voice_HandleUndo(void);
void Voice_HandlePending(const char *code6);
void Voice_HandleRetry(void);
void Voice_HandleWake(void);
void Voice_HandleChatWake(void);
void Chat_HandleKeyword(uint8_t code);
void Chat_HandleResponse(const char *reply);
void Voice_TouchDigit(uint8_t val);
void Voice_TouchReset(void);

#endif /* __APP_VOICE_H */
