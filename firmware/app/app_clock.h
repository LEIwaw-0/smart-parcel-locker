#ifndef __APP_CLOCK_H
#define __APP_CLOCK_H

/******************************************************
**  app_clock.h - wall clock + weather cards on the main screen.
**
**  There is no RTC on the board.  Clock_Weather_Tick() borrows the
**  ESP8266 TCP link once every ~180s, does a single HTTP GET to the
**  weather feed, takes the wall clock from the reply's "Date:" header
**  and then keeps time with the free-running DWT cycle counter (168MHz).
**
**  Only three functions are needed outside this module; the date maths,
**  the JSON scraping and the UTF-8 -> GBK table are private helpers.
******************************************************/
#include "app_config.h"

void Clock_DrawCard(void);      /* clock card, main screen  */
void Wx_DrawCard(void);         /* weather card, main screen */
void Clock_Weather_Tick(void);  /* one ~5ms tick of the fetch state machine */

#endif /* __APP_CLOCK_H */
