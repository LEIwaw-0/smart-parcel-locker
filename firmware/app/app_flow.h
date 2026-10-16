#ifndef __APP_FLOW_H
#define __APP_FLOW_H

/******************************************************
**  app_flow.h - the user-facing flows driven by the state machine.
**
**  Each State_*() handler consumes one debounced touch sample
**  (td_held + last touch point) and advances s_state.  main() only
**  dispatches on s_state and the Voice_*() helpers call back into
**  these handlers when a voice command is equivalent to a touch.
******************************************************/
#include "app_config.h"

void State_Admin(uint8_t td_held, uint16_t lx, uint16_t ly);
void State_Menu(uint8_t td_held, uint16_t lx, uint16_t ly);
void State_DepositKeypad(uint8_t td_held, uint16_t lx, uint16_t ly);
void State_PickupKeypad(uint8_t td_held, uint16_t lx, uint16_t ly);
void State_DoneBack(uint8_t td_held, uint16_t lx, uint16_t ly);

/* pickup-keypad helpers also reachable from the voice path */
void Kpad_AnnounceConfirm(void);
void Kpad_DoPickup(void);

#endif /* __APP_FLOW_H */
