/******************************************************
**  app_voice.c - LD3320 voice front end and the AI chat mode.
**
**  Split out of the monolithic main.c; bodies are unchanged.
******************************************************/
#include "app_voice.h"
#include "app_ctx.h"
#include "app_door.h"     /* DoOpenBox */
#include "app_ui.h"
#include "codebox.h"
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
    { 0x40, "你好",      "你好我是小智智能快递柜请问需要什么帮助" },
    { 0x41, "讲个笑话",   "离线模式讲不了笑话请连我WiFi试一下吧" },
    { 0x42, "再见",      "再见欢迎下次再来" },
    { 0x43, "谢谢",      "不客气随时为你服务" },
    { 0x44, "开启",      "好的已经开启" },
    { 0x45, "关闭",      "好的已经关闭" },
    { 0x47, "你是谁",    "我是小智智能快递柜助手" },
    { 0x48, "今天天气",   "抱歉没联网查不了天气" },
    { 0x49, "现在几点",   "抱歉没联网查不了时间" },
    { 0x4A, "取件",      "" },  /* empty reply: jump to pickup */
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
        (loop_tick - s_last_vtick) < VOICE_DEDUP_TICKS)
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
        StatusBar_Set(SRC_VOICE, 0, "取件码无效");
        SYN_FrameInfo(0, (uint8_t *)"[v8][t5][m0]识别无效请重新说");
        if (s_state == ST_MENU)
        {
            if (s_overlay && !s_chat_mode) Guide_ShowPartial();
            else                           Menu_DrawVoxHint("取件码无效 请说小樱");
        }
        return;
    }

    s_res_box  = box;
    s_res_ok   = 1;
    s_door_flow[box - 1]  = 2;
    s_door_src[box - 1]   = SRC_VOICE;
    s_door_phase[box - 1] = DOOR_W_OPEN; s_door_open_tick[box - 1] = loop_tick;
    s_door_db[box - 1]    = 0;

    strncpy(s_last_vcode, code6, 6);
    s_last_vcode[6] = 0;
    s_last_vtick = loop_tick;

    sprintf(msg, "box=%d", box);
    StatusBar_Set(SRC_VOICE, box, msg);

    DoOpenBox(box, SRC_VOICE);
    SYN_FrameInfo(0, (uint8_t *)"[v8][t5][m0]门已开请取件");
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
    StatusBar_Set(SRC_VOICE, 0, "请输入");
    Voice_ShowPartial();
}

void Voice_HandleUndo(void)
{
    if (s_vplen > 0)
    {
        s_vplen--;
        s_vpart[s_vplen] = 0;
        printf("[VOICE] UNDO %d left\r\n", (int)s_vplen);
        SYN_FrameInfo(0, (uint8_t *)"[v8][t5][m0]请说");
        StatusBar_Set(SRC_VOICE, 0, "请输入一位");
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
    strcat(tts, "请输入取件码");
    for (i = 0; i < 6; i++)
        strcat(tts, cn_digit[(uint8_t)(code6[i] - '0')]);
    strcat(tts, ",");
    SYN_FrameInfo(0, (uint8_t *)tts);

    StatusBar_Set(SRC_VOICE, 0, "请确认对或错");
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
    StatusBar_Set(SRC_VOICE, 0, "请输入");
    SYN_FrameInfo(0, (uint8_t *)"[v8][t5][m0]请说");
    if (s_state == ST_MENU)
        Menu_DrawVoxHint("请输入");
    printf("[VOICE] retry\r\n");
}

void Voice_HandleWake(void)
{
    s_chat_mode = 0;
    s_chat_busy = 0;
    s_vplen    = 0;
    s_vpart[0] = 0;
    StatusBar_Set(SRC_VOICE, 0, "请输入");
    /* SHORT cue (~1s): digit entry can start quickly. Full instructions
       stay on the screen hint so the TTS does not need to say them. */
    SYN_FrameInfo(0, (uint8_t *)"[v8][t5][m0]请重新说");
    if (s_state == ST_MENU) {
        Menu_DrawPickupGuide();

    }
    printf("[VOICE] wake\r\n");
}

void Voice_HandleChatWake(void)
{
    s_chat_mode = 1;
    s_chat_busy = 0;
    s_chat_tick = loop_tick;
    StatusBar_Set(SRC_VOICE, 0, "对话模式 说小智提问");
    SYN_FrameInfo(0, (uint8_t *)"[v8][t5][m0]你好我在请提问");
    if (s_state == ST_MENU) {
        Menu_DrawChatPanel();
        Menu_DrawVoxHint("说关键词 小智听到");
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
    s_chat_tick = loop_tick;
    printf("[CHAT] kw[%d]:%s\r\n", code, text);

    /* special keyword: "取" jumps straight to the pickup keypad */
    if (code == 0x4A)
    {
        s_chat_mode = 0;
        s_overlay = 0;
        s_chat_busy = 0;
        s_state = ST_PICKUP_KEYPAD;
        s_overlay = 0;
        Keypad_Begin("取件", "请输入六位数字取件码");
        StatusBar_Set(SRC_VOICE, 0, "请输入取件码");
        return;
    }

    strcpy(sendbuf, "CHAT:");
    strcat(sendbuf, text);
    strcat(sendbuf, "\n");
    if (ESP8266_IsReady() && !s_chat_busy)
    {
        if (ESP8266_SendText(sendbuf))
        {
            s_chat_busy = 1;
            StatusBar_Set(SRC_VOICE, 0, "请输入");
            if (s_state == ST_MENU) Menu_DrawVoxHint(text);
        }
        else SYN_FrameInfo(0, (uint8_t *)"[v8][t5][m0]网络繁忙请稍候");
    }
    else
    {
        strcpy(tts, "[v8][t5][m0]");
        strcat(tts, s_chat_table[i].reply);
        if (s_chat_table[i].reply[0]) SYN_FrameInfo(0, (uint8_t *)tts);
        if (!ESP8266_IsReady()) StatusBar_Set(SRC_VOICE, 0, "未连网对话模式");
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
    StatusBar_Set(SRC_VOICE, 0, "对话模式 说小智提问");
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
    StatusBar_Set(SRC_TOUCH, 0, "已重输");
}
