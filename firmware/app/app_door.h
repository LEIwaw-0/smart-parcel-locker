#ifndef __APP_DOOR_H
#define __APP_DOOR_H

/******************************************************
**  app_door.h - lock drive + door-open feedback tracking.
**
**  DoOpenBox()  : turns the LCD backlight off, drives the relay and
**                 re-inits the LCD controller (relay EMI recovery).
**  Door_Tick()  : per-box state machine fed by the PCF8574 P4-P7
**                 feedback nibble: WAIT-OPEN -> WAIT-CLOSE -> DONE,
**                 with a 30s timeout for a box left ajar.
**  DoorStatus_Update() / DoorStatus_Draw() : the "band" widget shared
**                 by the DONE screens.
******************************************************/
#include "app_config.h"

void DoorStatus_Update(void);
void DoorStatus_Draw(uint16_t y);
void DoOpenBox(uint8_t box_id, uint8_t src);
void Door_Tick(void);

#endif /* __APP_DOOR_H */
