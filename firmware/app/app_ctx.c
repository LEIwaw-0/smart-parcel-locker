/******************************************************
**  app_ctx.c - definitions of the shared state declared in app_ctx.h
**
**  Initial values are identical to the ones the variables carried
**  when they were file-scope statics in the monolithic main.c.
******************************************************/
#include "app_ctx.h"

uint8_t  s_state   = ST_MENU;
uint8_t  s_overlay = 0;      /* 1 = chat / pickup-guide overlay shown */

/* Result info shared between flow and DONE screen */
uint8_t  s_res_box = 0;
char     s_res_code[8];
uint8_t  s_res_ok = 0;       /* 1 = success, 0 = not found / full */
char     s_res_msg[32];

/* Menu buttons */
Button_t s_btn_back;
Button_t s_btn_continue;
Button_t s_pg_btns[12];
Button_t s_btn_admin_back;
uint8_t  s_pick_pressed = 0;

/* Voice */
char     s_vpart[7] = {0};
uint8_t  s_vplen    = 0;
uint8_t  s_voice_pending = 0;

/* Pickup keypad confirm latch */
uint8_t  s_kpad_confirm = 0;

/* Chat mode */
uint8_t  s_chat_mode = 0;
uint8_t  s_chat_busy = 0;
uint32_t s_chat_tick = 0;

/* Chinese digits for the conversational TTS readback (GBK literals) */
const char *const cn_digit[10] = {
    "0","\xD2\xBB","\xB6\xFE","\xC8\xFD","\xCB\xC4","\xCE\xE5","\xC1\xF9","\xC6\xDF","\xB0\xCB","\xBE\xC5"
};
