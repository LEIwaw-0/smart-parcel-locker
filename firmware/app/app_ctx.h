#ifndef __APP_CTX_H
#define __APP_CTX_H

/******************************************************
**  app_ctx.h - shared mutable state (the old file-scope statics)
**
**  The original firmware kept every variable as a file-scope
**  static inside one main.c, so no module boundary mattered.
**  After the split a variable that two modules both touch has to
**  be defined exactly once and declared everywhere else.
**
**  Only genuinely cross-module variables live here; anything a
**  single module owns stayed private static inside that module
**  (see s_last_msg / s_clk_* / s_wx_* / s_band_* below).
******************************************************/
#include "app_config.h"
#include "button.h"     /* Button_t */

/* UI state machine (ST_*) and the overlay flag */
extern uint8_t  s_state;
extern uint8_t  s_overlay;

/* result block shared by the flow, the DONE screens and the door tracker */
extern uint8_t  s_res_box;
extern char     s_res_code[8];
extern uint8_t  s_res_ok;
extern char     s_res_msg[32];

/* on-screen buttons shared between the menu and the state handlers */
extern Button_t s_btn_back;
extern Button_t s_btn_continue;      /* pickup-done: pick another           */
extern Button_t s_pg_btns[12];       /* pickup guide: 0-9, undo, reset      */
extern Button_t s_btn_admin_back;
extern uint8_t  s_pick_pressed;

/* voice capture buffer + conversational state */
extern char     s_vpart[7];
extern uint8_t  s_vplen;
extern uint8_t  s_voice_pending;     /* 1 = waiting for the dui/cuo answer  */

/* pickup keypad: 1 = 6 digits filled, waiting for confirm */
extern uint8_t  s_kpad_confirm;

/* AI chat-mode lease */
extern uint8_t  s_chat_mode;
extern uint8_t  s_chat_busy;
extern uint32_t s_chat_tick;

/* The per-box door state now lives inside app_door.c (single writer: the
   door task) and reaches the screen through Door_ShadowSet() below. */

/* Chinese digits for the conversational TTS readback (GBK literals) */
extern const char *const cn_digit[10];

#endif /* __APP_CTX_H */
