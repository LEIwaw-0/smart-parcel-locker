#ifndef __APP_CLOCK_H
#define __APP_CLOCK_H

/******************************************************
**  app_clock.h - wall clock + weather cards on the main screen.
**
**  There is no RTC on the board.  The clock task borrows the ESP8266 TCP
**  link once every ~180s (via the link task, see Clock_Task), does a single
**  HTTP GET to the weather feed, takes the wall clock from the reply's
**  "Date:" header and then keeps time with the free-running DWT cycle
**  counter (168MHz).
**
**  Clock_DrawCard() / Wx_DrawCard() redraw the two main-screen cards only;
**  app_ui.c calls them from Menu_Draw().  The date maths, the JSON scraping
**  and the UTF-8 -> GBK table are private helpers.
******************************************************/
#include "app_config.h"

void Clock_DrawCard(void);      /* clock card, main screen  */
void Wx_DrawCard(void);         /* weather card, main screen */

/* the HTTP body of the weather fetch; the link task polls straight into it
   and the clock task parses it out of there once the q_clock reply lands */
char    *Clock_WxBuf(void);
uint16_t Clock_WxBufSize(void);

void Clock_Task(void *arg);

#endif /* __APP_CLOCK_H */
