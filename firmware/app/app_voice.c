/******************************************************
**  app_voice.c - LD3320 voice front end and the AI chat mode.
**
**  Voice_Task() is a thin demux: it polls the LD3320 driver and republishes
**  every one-shot event as an EV_VOICE message on q_ui.  Everything that
**  reacts to voice (the keypad, the state machine, the status bar, the TTS)
**  belongs to the UI task and runs in Voice_ApplyEvt().
**
**  Split out of the monolithic main.c; the decision logic is unchanged.
******************************************************/
#include "app_voice.h"
#include "app_ctx.h"
#include "app_rtos.h"
#include "app_cloud.h"    /* Cloud_Queue */
#include "app_door.h"     /* DoOpenBox */
#include "app_ui.h"
#include "codebox.h"
#include "ld3320.h"
#include "esp8266.h"
#include "keypad.h"
#include "syn6288.h"
#include <stdio.h>
#include <string.h>

/* ---- private state: 8s de-dup guard against STC11 retries ---- */
static char     s_last_vcode[7] = {0};
static uint32_t s_last_vtick    = 0xFFFFFFFFu;

/* ---- private state: offline keyword chat table ---- */
typedef struct { uint8_t code; const char *text; const char *reply; } chat_kw_t;
static const chat_kw_t s_chat_table[] = {
    { 0x40, "\xC4\xE3\xBA\xC3",      "\xC4\xE3\xBA\xC3\xCE\xD2\xCA\xC7\xD0\xA1\xD6\xC7\xD6\xC7\xC4\xDC\xBF\xEC\xB5\xDD\xB9\xF1\xC7\xEB\xCE\xCA\xD0\xE8\xD2\xAA\xCA\xB2\xC3\xB4\xB0\xEF\xD6\xFA" },
    { 0x41, "\xBD\xB2\xB8\xF6\xD0\xA6\xBB\xB0",   "\xC0\xEB\xCF\xDF\xC4\xA3\xCA\xBD\xBD\xB2\xB2\xBB\xC1\xCB\xD0\xA6\xBB\xB0\xC7\xEB\xC1\xAC\xCE\xD2WiFi\xCA\xD4\xD2\xBB\xCF\xC2\xB0\xC9" },
    { 0x42, "\xD4\xD9\xBC\xFB",      "\xD4\xD9\xBC\xFB\xBB\xB6\xD3\xAD\xCF\xC2\xB4\xCE\xD4\xD9\xC0\xB4" },
    { 0x43, "\xD0\xBB\xD0\xBB",      "\xB2\xBB\xBF\xCD\xC6\xF8\xCB\xE6\xCA\xB1\xCE\xAA\xC4\xE3\xB7\xFE\xCE\xF1" },
    { 0x44, "\xBF\xAA\xC6\xF4",      "\xBA\xC3\xB5\xC4\xD2\xD1\xBE\xAD\xBF\xAA\xC6\xF4" },
    { 0x45, "\xB9\xD8\xB1\xD5",      "\xBA\xC3\xB5\xC4\xD2\xD1\xBE\xAD\xB9\xD8\xB1\xD5" },
    { 0x47, "\xC4\xE3\xCA\xC7\xCB\xAD",    "\xCE\xD2\xCA\xC7\xD0\xA1\xD6\xC7\xD6\xC7\xC4\xDC\xBF\xEC\xB5\xDD\xB9\xF1\xD6\xFA\xCA\xD6" },
    { 0x48, "\xBD\xF1\xCC\xEC\xCC\xEC\xC6\xF8",   "\xB1\xA7\xC7\xB8\xC3\xBB\xC1\xAA\xCD\xF8\xB2\xE9\xB2\xBB\xC1\xCB\xCC\xEC\xC6\xF8" },
    { 0x49, "\xCF\xD6\xD4\xDA\xBC\xB8\xB5\xE3",   "\xB1\xA7\xC7\xB8\xC3\xBB\xC1\xAA\xCD\xF8\xB2\xE9\xB2\xBB\xC1\xCB\xCA\xB1\xBC\xE4" },
    { 0x4A, "\xC8\xA1\xBC\xFE",      "" },  /* empty reply: jump to pickup */
};
#define CHAT_KW_COUNT  (sizeof(s_chat_table) / sizeof(s_chat_table[0]))

void Voice_HandleCode(const char *code6)
{
    uint8_t box;
    uint8_t phone6[PHONE_LEN];
    uint8_t i;
    char msg[24];

    /* de-dup: same code within 8s is treated as a repeat (TTS self-heard,
       STC11 retry, double utterance) */
    if (s_last_vtick != 0xFFFFFFFFu &&
        strncmp(s_last_vcode, code6, 6) == 0 &&
        (App_Ms() - s_last_vtick) < VOICE_DEDUP_MS)
    {
        printf("[VOICE] dup %s\r\n", code6);
        return;
    }

    for (i = 0; i < PHONE_LEN; i++) phone6[i] = (uint8_t)code6[i];

    box = CodeBox_PickupByCode(phone6);
    if (box == 0)
        box = CodeBox_PickupByPhone(phone6);

    if (box == 0)
    {
        s_voice_pending = 0;
        StatusBar_Set(SRC_VOICE, 0, "\xC8\xA1\xBC\xFE\xC2\xEB\xCE\xDE\xD0\xA7");
        SYN_FrameInfo(0, (uint8_t *)"[v8][t5][m0]\xCA\xB6\xB1\xF0\xCE\xDE\xD0\xA7\xC7\xEB\xD6\xD8\xD0\xC2\xCB\xB5");
        if (s_state == ST_MENU)
        {
            if (s_overlay && !s_chat_mode) Guide_ShowPartial();
            else                           Menu_DrawVoxHint("\xC8\xA1\xBC\xFE\xC2\xEB\xCE\xDE\xD0\xA7 \xC7\xEB\xCB\xB5\xD0\xA1\xD3\xA3");
        }
        return;
    }

    s_res_box  = box;
    s_res_ok   = 1;

    strncpy(s_last_vcode, code6, 6);
    s_last_vcode[6] = 0;
    s_last_vtick = App_Ms();

    sprintf(msg, "box=%d", box);
    StatusBar_Set(SRC_VOICE, box, msg);

    DoOpenBox(box, SRC_VOICE, 2);
    SYN_FrameInfo(0, (uint8_t *)"[v8][t5][m0]\xC3\xC5\xD2\xD1\xBF\xAA\xC7\xEB\xC8\xA1\xBC\xFE");
    s_overlay = 0;
    s_chat_mode = 0;
    s_state = ST_PICKUP_DONE;
    PickupDone_Draw();
    printf("[VOICE] pickup code=%s box=%d OK\r\n", code6, box);
}

static void Voice_ShowPartial(void)
{
    char hint[24];
    char shown[8];
    uint8_t i;
    for (i = 0; i < 6; i++)
        shown[i] = (i < s_vplen) ? s_vpart[i] : '_';
    shown[6] = 0;
    sprintf(hint, "code:%s", shown);
    if (s_state == ST_MENU)
    {
        if (s_overlay && !s_chat_mode) Guide_ShowPartial();
        else                           Menu_DrawVoxHint(hint);
    }

}

void Voice_HandleDigit(uint8_t idx, uint8_t val)
{
    char tts[20];
    if (idx >= 6) return;
    s_vpart[idx] = (char)('0' + val);
    if (idx >= s_vplen) s_vplen = (uint8_t)(idx + 1);
    printf("[VOICE] digit idx=%d val=%d\r\n", (int)(idx+1), (int)val);
    if (idx < 5)
    {
        strcpy(tts, "[v8][t5][m0]");
        strcat(tts, cn_digit[val]);
        SYN_FrameInfo(0, (uint8_t *)tts);
    }
    StatusBar_Set(SRC_VOICE, 0, "\xC7\xEB\xCA\xE4\xC8\xEB");
    Voice_ShowPartial();
}

void Voice_HandleUndo(void)
{
    if (s_vplen > 0)
    {
        s_vplen--;
        s_vpart[s_vplen] = 0;
        printf("[VOICE] UNDO %d left\r\n", (int)s_vplen);
        SYN_FrameInfo(0, (uint8_t *)"[v8][t5][m0]\xC7\xEB\xCB\xB5");
        StatusBar_Set(SRC_VOICE, 0, "\xC7\xEB\xCA\xE4\xC8\xEB\xD2\xBB\xCE\xBB");
        Voice_ShowPartial();
    }
}

void Voice_HandlePending(const char *code6)
{
    char tts[64];
    char hint[24];
    uint8_t i;

    s_vplen = 6;
    strncpy(s_vpart, code6, 6);
    s_vpart[6] = 0;

    strcpy(tts, "[v8][t5][m0]");
    strcat(tts, "\xC7\xEB\xCA\xE4\xC8\xEB\xC8\xA1\xBC\xFE\xC2\xEB");
    for (i = 0; i < 6; i++)
        strcat(tts, cn_digit[(uint8_t)(code6[i] - '0')]);
    strcat(tts, ",");
    SYN_FrameInfo(0, (uint8_t *)tts);

    StatusBar_Set(SRC_VOICE, 0, "\xC7\xEB\xC8\xB7\xC8\xCF\xB6\xD4\xBB\xF2\xB4\xED");
    if (s_state == ST_MENU)
    {
        sprintf(hint, "code:%s", code6);
        Menu_DrawVoxHint(hint);
    }
    printf("[KEYPAD] done %s\r\n", code6);
}

void Voice_HandleRetry(void)
{
    s_vplen = 0;
    s_vpart[0] = 0;
    StatusBar_Set(SRC_VOICE, 0, "\xC7\xEB\xCA\xE4\xC8\xEB");
    SYN_FrameInfo(0, (uint8_t *)"[v8][t5][m0]\xC7\xEB\xCB\xB5");
    if (s_state == ST_MENU)
        Menu_DrawVoxHint("\xC7\xEB\xCA\xE4\xC8\xEB");
    printf("[VOICE] retry\r\n");
}

void Voice_HandleWake(void)
{
    s_chat_mode = 0;
    s_chat_busy = 0;
    s_vplen    = 0;
    s_vpart[0] = 0;
    StatusBar_Set(SRC_VOICE, 0, "\xC7\xEB\xCA\xE4\xC8\xEB");
    /* SHORT cue (~1s): digit entry can start quickly. Full instructions
       stay on the screen hint so the TTS does not need to say them. */
    SYN_FrameInfo(0, (uint8_t *)"[v8][t5][m0]\xC7\xEB\xD6\xD8\xD0\xC2\xCB\xB5");
    if (s_state == ST_MENU) {
        Menu_DrawPickupGuide();

    }
    printf("[VOICE] wake\r\n");
}

void Voice_HandleChatWake(void)
{
    s_chat_mode = 1;
    s_chat_busy = 0;
    s_chat_tick = App_Ms();
    StatusBar_Set(SRC_VOICE, 0, "\xB6\xD4\xBB\xB0\xC4\xA3\xCA\xBD \xCB\xB5\xD0\xA1\xD6\xC7\xCC\xE1\xCE\xCA");
    SYN_FrameInfo(0, (uint8_t *)"[v8][t5][m0]\xC4\xE3\xBA\xC3\xCE\xD2\xD4\xDA\xC7\xEB\xCC\xE1\xCE\xCA");
    if (s_state == ST_MENU) {
        Menu_DrawChatPanel();
        Menu_DrawVoxHint("\xCB\xB5\xB9\xD8\xBC\xFC\xB4\xCA \xD0\xA1\xD6\xC7\xCC\xFD\xB5\xBD");
    }
    printf("[VOICE] chat wake\r\n");
}

void Chat_HandleKeyword(uint8_t code)
{
    const char *text = 0;
    uint8_t i;
    char tts[80];
    char sendbuf[32];
    for (i = 0; i < CHAT_KW_COUNT; i++)
        if (s_chat_table[i].code == code) { text = s_chat_table[i].text; break; }
    if (!text) { printf("[CHAT] unknown code=%d\r\n", code); return; }
    s_chat_tick = App_Ms();
    printf("[CHAT] kw[%d]:%s\r\n", code, text);

    /* special keyword: "取" jumps straight to the pickup keypad */
    if (code == 0x4A)
    {
        s_chat_mode = 0;
        s_overlay = 0;
        s_chat_busy = 0;
        s_state = ST_PICKUP_KEYPAD;
        s_overlay = 0;
        Keypad_Begin("\xC8\xA1\xBC\xFE", "\xC7\xEB\xCA\xE4\xC8\xEB\xC1\xF9\xCE\xBB\xCA\xFD\xD7\xD6\xC8\xA1\xBC\xFE\xC2\xEB");
        StatusBar_Set(SRC_VOICE, 0, "\xC7\xEB\xCA\xE4\xC8\xEB\xC8\xA1\xBC\xFE\xC2\xEB");
        return;
    }

    strcpy(sendbuf, "CHAT:");
    strcat(sendbuf, text);
    strcat(sendbuf, "\n");
    if (g_link_up && !s_chat_busy)
    {
        /* hand the line to the link task; it owns the ESP8266 send buffer */
        Cloud_Queue(sendbuf);
        s_chat_busy = 1;
        StatusBar_Set(SRC_VOICE, 0, "\xC7\xEB\xCA\xE4\xC8\xEB");
        if (s_state == ST_MENU) Menu_DrawVoxHint(text);
    }
    else
    {
        strcpy(tts, "[v8][t5][m0]");
        strcat(tts, s_chat_table[i].reply);
        if (s_chat_table[i].reply[0]) SYN_FrameInfo(0, (uint8_t *)tts);
        if (!g_link_up) StatusBar_Set(SRC_VOICE, 0, "\xCE\xB4\xC1\xAC\xCD\xF8\xB6\xD4\xBB\xB0\xC4\xA3\xCA\xBD");
    }
}

void Chat_HandleResponse(const char *reply)
{
    char tts[160];
    s_chat_busy = 0;
    printf("[AI] reply: %s\r\n", reply);
    const char *ai_msg = reply;
    if (strncmp(reply, "AI:", 3) == 0) ai_msg = reply + 3;
    if (ai_msg[0])
    {
        strcpy(tts, "[v8][t5][m0]");
        strncat(tts, ai_msg, sizeof(tts) - strlen(tts) - 1);
        SYN_FrameInfo(0, (uint8_t *)tts);
    }
    StatusBar_Set(SRC_VOICE, 0, "\xB6\xD4\xBB\xB0\xC4\xA3\xCA\xBD \xCB\xB5\xD0\xA1\xD6\xC7\xCC\xE1\xCE\xCA");
}

void Voice_TouchDigit(uint8_t val)
{
    if (s_vplen >= 6) return;
    Voice_HandleDigit(s_vplen, val);
    if (s_vplen >= 6)
    {
        s_vpart[6] = 0;  /* Voice_HandleCode clears overlay on success */
        Voice_HandleCode(s_vpart);
    }
}

void Voice_TouchReset(void)
{
    s_vplen = 0;
    s_vpart[0] = 0;
    s_vpart[6] = 0;
    Guide_ShowPartial();
    StatusBar_Set(SRC_TOUCH, 0, "\xD2\xD1\xD6\xD8\xCA\xE4");
}

/******************************************************
**  the voice task
**
**  A pure demux.  The LD3320 driver keeps one-shot flags; this task polls
**  them every 5ms, echoes every raw line on USART1 for wiring checks and
**  republishes each event as an EV_VOICE message.  Nothing is decided here:
**  the UI task knows which screen is up and applies the event.
**
**  Only ONE of the action flags is consumed per iteration - that is the
**  old superloop's if/else chain, kept bit-for-bit.
******************************************************/
void Voice_Task(void *arg)
{
    app_evt_t ev;

    (void)arg;

    for (;;)
    {
        char    raw[24];
        char    code[7];
        uint8_t didx, dval, ccode;
        int     cmd;

        ev.kind = EV_VOICE;
        ev.b = 0; ev.c = 0; ev.x = 0; ev.y = 0; ev.text[0] = 0;

        /* a) echo EVERY raw line so wiring/firmware can be checked */
        if (LD3320_GetRaw(raw, sizeof(raw)))
        {
            uint8_t show;

            g_voice_seen = 1;
            printf("[LD3320] ");
            if (strncmp(raw, "DIGIT:", 6) == 0)
                printf("digit pos=%d val=%c", (int)(raw[6]-'0')+1, raw[12]);
            else if (strncmp(raw, "CMD:00", 6) == 0)
                printf("CMD:00 wake");
            else if (strncmp(raw, "CMD:FE", 6) == 0)
                printf("CMD:FE timeout");
            else if (strncmp(raw, "CHAT:WAKE", 9) == 0)
                printf("CHAT:WAKE");
            else if (strncmp(raw, "CHAT:", 5) == 0)
                printf("CHAT 0x%02X%02X", raw[5], raw[6]);
            else if (strncmp(raw, "PCODE:", 6) == 0)
                printf("PCODE %s", raw+6);
            else if (strncmp(raw, "CODE:", 5) == 0)
                printf("CODE %s", raw+5);
            else if (strncmp(raw, "RETRY", 5) == 0)
                printf("RETRY");
            else if (strncmp(raw, "UNDO", 4) == 0)
                printf("UNDO");
            else if (strncmp(raw, "HB", 2) == 0)
                printf("HB");
            else if (strncmp(raw, "LD-DIGIT", 8) == 0)
                printf("\xC4\xA3\xBF\xE9\xBE\xCD\xD0\xF7");
            else if (strncmp(raw, "LD_OK", 5) == 0)
                printf("\xD7\xD4\xBC\xEC\xCD\xA8\xB9\xFD %s", raw+5);
            else if (strncmp(raw, "LD_ERR", 6) == 0)
                printf("\xD7\xD4\xBC\xEC\xCA\xA7\xB0\xDC");
            else
                printf("%s", raw);
            printf("\r\n");

            /* boot beacons/selftest lines go to serial only; only
               recognition results are shown on the menu hint line */
            show = (strncmp(raw, "LD3320", 6) != 0 &&
                    strncmp(raw, "LD-DIGIT", 8) != 0 &&
                    strncmp(raw, "LD_OK",  5) != 0 &&
                    strncmp(raw, "LD_ERR", 6) != 0 &&
                    strncmp(raw, "PCODE:", 6) != 0 &&  /* dedicated panel */
                    strncmp(raw, "RETRY",  5) != 0 &&   /* dedicated panel */
                    strncmp(raw, "HB",     2) != 0);    /* heartbeat: serial only */
            if (show)
            {
                ev.a = VS_RAW;
                if (strncmp(raw, "DIGIT:", 6) == 0)
                {
                    /* digit capture progress: "DIGIT:n VAL:x" */
                    uint8_t idx = (uint8_t)(raw[6] - '0');   /* 0..5 */
                    char    val = raw[12];                   /* value digit */
                    sprintf(ev.text, "%d:%c", (int)(idx + 1), val);
                    ev.b = 1;                                /* keep ~5s */
                }
                else
                {
                    strncpy(ev.text, raw, APP_EV_TEXT_MAX - 1);
                    ev.text[APP_EV_TEXT_MAX - 1] = 0;
                }
                App_UiPost(&ev);
            }
        }

        /* b) chat-mode wake ("xiao zhi") */
        if (LD3320_GetChatWake())
        {
            ev.a = VS_CHAT_WAKE;
            ev.b = 0;
            ev.c = 0;
            ev.text[0] = 0;
            App_UiPost(&ev);
        }

        /* c) one-shot voice actions */
        if (LD3320_GetDigit(&didx, &dval))
        {
            ev.a = VS_DIGIT;
            ev.b = didx;
            ev.c = dval;
            ev.text[0] = 0;
            App_UiPost(&ev);
        }
        else if (LD3320_GetUndo())
        {
            ev.a = VS_UNDO;
            ev.b = 0; ev.c = 0; ev.text[0] = 0;
            App_UiPost(&ev);
        }
        else if (LD3320_GetPendingCode(code))
        {
            ev.a = VS_PENDING;
            ev.b = 0; ev.c = 0;
            strncpy(ev.text, code, APP_EV_TEXT_MAX - 1);
            ev.text[APP_EV_TEXT_MAX - 1] = 0;
            App_UiPost(&ev);
        }
        else if (LD3320_GetRetry())
        {
            ev.a = VS_RETRY;
            ev.b = 0; ev.c = 0; ev.text[0] = 0;
            App_UiPost(&ev);
        }
        else if (LD3320_GetCode(code))
        {
            ev.a = VS_CODE;
            ev.b = 0; ev.c = 0;
            strncpy(ev.text, code, APP_EV_TEXT_MAX - 1);
            ev.text[APP_EV_TEXT_MAX - 1] = 0;
            App_UiPost(&ev);
        }
        else if ((cmd = LD3320_GetCmd()) >= 0)
        {
            ev.a = VS_CMD;
            ev.b = (uint8_t)cmd;
            ev.c = 0; ev.text[0] = 0;
            App_UiPost(&ev);
        }

        /* d) chat keyword */
        if (LD3320_GetChat(&ccode))
        {
            ev.a = VS_CHAT;
            ev.b = ccode;
            ev.c = 0; ev.text[0] = 0;
            App_UiPost(&ev);
        }

        vTaskDelay(pdMS_TO_TICKS(5));
    }
}
