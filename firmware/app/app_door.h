#ifndef __APP_DOOR_H
#define __APP_DOOR_H

/******************************************************
**  app_door.h - lock drive + door-open feedback tracking.
**
**  DoOpenBox(box, src, flow) : the UI-side entry point.  It kills the LCD
**                 backlight, hands the open request to the door task over
**                 q_door (the task is the only writer of the relay state)
**                 and re-inits the LCD controller (relay EMI recovery).
**  Door_ShadowSet() : the UI task records the phase/flow it learned from
**                 an EV_DOOR_STATE event, so the band widget can follow
**                 the door state without touching the door task's state.
**  Door_Task()  : relay auto-off watchdog (20ms), PCF8574 P4-P7 feedback
**                 poll (200ms) and the per-box WAIT-OPEN -> WAIT-CLOSE ->
**                 DONE state machine, with a 30s timeout for a box left
**                 ajar.  Completions are reported as EV_DOOR_DONE.
**  DoorStatus_Update() / DoorStatus_Draw() : the "band" widget shared
**                 by the DONE screens (UI side, draws under m_lcd).
******************************************************/
#include "app_config.h"

void DoorStatus_Update(void);
void DoorStatus_Draw(uint16_t y);
void Door_ShadowSet(uint8_t box, uint8_t phase, uint8_t flow);
void DoOpenBox(uint8_t box_id, uint8_t src, uint8_t flow);
void Door_Task(void *arg);

#endif /* __APP_DOOR_H */
