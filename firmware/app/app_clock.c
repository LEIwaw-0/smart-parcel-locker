/******************************************************
**  app_clock.c - wall clock + weather cards.
**
**  No RTC on the board: the clock is seeded from the HTTP "Date:"
**  header of one weather fetch and then kept by the free-running DWT
**  cycle counter (168MHz).  The date maths, the JSON scraping and the
**  UTF-8 -> GBK word table are all private to this module.
**
**  Split out of the monolithic main.c; bodies are unchanged.
******************************************************/
#include "app_clock.h"
#include "app_ctx.h"
#include "app_ui.h"       /* TitleBar_DrawLink */
#include "esp8266.h"
#include "lcd.h"
#include <stdio.h>
#include <string.h>

/* ---- private state ---- */
static uint8_t  s_clk_valid = 0;
static int32_t  s_clk_year  = 2026;
static int32_t  s_clk_mon   = 1;
static int32_t  s_clk_day   = 1;
static uint8_t  s_clk_wday  = 0;      /* 0 = Sunday */
static int32_t  s_clk_hour  = 0;
static int32_t  s_clk_min   = 0;
static int32_t  s_clk_sec   = 0;
static uint32_t s_cyc_last  = 0;
static uint32_t s_cyc_acc   = 0;

static uint8_t  s_wx_valid  = 0;
static char     s_wx_temp[8]  = "--";
static char     s_wx_humi[8]  = "--";
static char     s_wx_wind[16] = "";
static char     s_wx_wlev[8]  = "";
static uint32_t s_wx_next   = WX_FIRST_TICKS;
static char     s_wx_rx[1100];

static const char *const MON3[12] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

/* ---- UTF-8 -> GBK for the handful of weather words we display ---- */
typedef struct { uint16_t u; uint16_t g; } u2g_t;
static const u2g_t U2G[] = {
    { 0x4E1C, 0xB6AB },  /* 东 */
    { 0x5357, 0xC4CF },  /* 南 */
    { 0x897F, 0xCEF7 },  /* 西 */
    { 0x5317, 0xB1B1 },  /* 北 */
    { 0x98CE, 0xB7E7 },  /* 风 */
    { 0x7EA7, 0xBCB6 },  /* 级 */
    { 0x5FAE, 0xCEA2 },  /* 微 */
    { 0x65E0, 0xCEDE },  /* 无 */
    { 0x6301, 0xB3D6 },  /* 持 */
    { 0x7EED, 0xD0F8 },  /* 续 */
    { 0x8F6C, 0xD7AA },  /* 转 */
    { 0x5411, 0xCFF2 }   /* 向 */
};

static int32_t DaysFromCivil(int32_t y, int32_t m, int32_t d)
{
    int32_t era, yoe, doy, doe;
    y -= (m <= 2);
    era = (y >= 0 ? y : y - 399) / 400;
    yoe = y - era * 400;
    doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

static void CivilFromDays(int32_t z, int32_t *py, int32_t *pm, int32_t *pd)
{
    int32_t era, doe, yoe, y, doy, mp, d, m;
    z += 719468;
    era = (z >= 0 ? z : z - 146096) / 146097;
    doe = z - era * 146097;
    yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    y   = yoe + era * 400;
    doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    mp  = (5 * doy + 2) / 153;
    d   = doy - (153 * mp + 2) / 5 + 1;
    m   = mp + (mp < 10 ? 3 : -9);
    y  += (m <= 2);
    *py = y; *pm = m; *pd = d;
}

static int32_t Str_Int(const char **pp)
{
    int32_t v = 0;
    const char *p = *pp;
    while (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); p++; }
    *pp = p;
    return v;
}

static int32_t MonthFromName(const char *p)
{
    int32_t i;
    for (i = 0; i < 12; i++)
        if (p[0] == MON3[i][0] && p[1] == MON3[i][1] && p[2] == MON3[i][2])
            return i + 1;
    return 0;
}

static uint8_t Clock_ParseDate(const char *resp)
{
    const char *p = strstr(resp, "Date: ");
    int32_t d, y, hh, mm, ss, mon, days, yy, mm2, dd2;
    if (!p) return 0;
    p += 6;
    while (*p && *p != ',') p++;          /* skip the weekday name */
    if (*p != ',') return 0;
    p++;
    while (*p == ' ') p++;
    d = Str_Int(&p);
    while (*p == ' ') p++;
    mon = MonthFromName(p);
    if (!mon) return 0;
    p += 3;
    while (*p == ' ') p++;
    y = Str_Int(&p);
    while (*p == ' ') p++;
    hh = Str_Int(&p);
    if (*p != ':') return 0;
    p++;
    mm = Str_Int(&p);
    if (*p != ':') return 0;
    p++;
    ss = Str_Int(&p);

    hh += 8;                              /* GMT -> Beijing time */
    if (hh >= 24) { hh -= 24; d += 1; }

    days = DaysFromCivil(y, mon, d);
    CivilFromDays(days, &yy, &mm2, &dd2); /* normalises a day overflow */
    s_clk_year = yy; s_clk_mon = mm2; s_clk_day = dd2;
    s_clk_hour = hh; s_clk_min = mm; s_clk_sec = ss;
    s_clk_wday = (uint8_t)(((days % 7) + 7 + 4) % 7);  /* 1970-01-01 = Thu */
    s_cyc_acc  = 0;
    s_clk_valid = 1;
    return 1;
}

static void Wx_TextToGbk(const char *src, char *dst, uint16_t dstsz)
{
    uint16_t o = 0;
    while (*src && (uint16_t)(o + 2) < dstsz)
    {
        uint8_t c = (uint8_t)(*src);
        if (c < 0x80)
        {
            dst[o++] = (char)c;
            src++;
        }
        else if ((c & 0xE0) == 0xE0 &&
                 ((uint8_t)src[1] & 0xC0) == 0x80 &&
                 ((uint8_t)src[2] & 0xC0) == 0x80)
        {
            uint16_t cp = (uint16_t)(((uint16_t)(c & 0x0F) << 12) |
                                     ((uint16_t)((uint8_t)src[1] & 0x3F) << 6) |
                                      (uint16_t)((uint8_t)src[2] & 0x3F));
            uint16_t g = 0;
            uint8_t  k;
            for (k = 0; k < (uint8_t)(sizeof(U2G) / sizeof(U2G[0])); k++)
                if (U2G[k].u == cp) { g = U2G[k].g; break; }
            if (g)
            {
                dst[o++] = (char)(uint8_t)(g >> 8);
                dst[o++] = (char)(uint8_t)(g & 0xFF);
            }
            src += 3;      /* unmapped ideographs are simply dropped */
        }
        else
        {
            src++;         /* stray byte */
        }
    }
    dst[o] = 0;
}

static uint8_t Wx_Field(const char *json, const char *key, char *out, uint16_t outsz)
{
    char pat[20];
    const char *p;
    uint16_t o = 0;
    if (strlen(key) + 4 > sizeof(pat)) return 0;
    sprintf(pat, "\"%s\":\"", key);
    p = strstr(json, pat);
    if (!p) return 0;
    p += strlen(pat);
    while (*p && *p != '"' && (uint16_t)(o + 1) < outsz) out[o++] = *p++;
    out[o] = 0;
    return 1;
}

static uint8_t Wx_Parse(const char *json)
{
    char tmp[24];
    char *dot;

    if (!Wx_Field(json, "temp", tmp, sizeof(tmp))) return 0;
    dot = strchr(tmp, '.');
    if (dot) *dot = 0;                    /* keep the integer part only */
    if (!tmp[0]) return 0;
    strncpy(s_wx_temp, tmp, sizeof(s_wx_temp) - 1);
    s_wx_temp[sizeof(s_wx_temp) - 1] = 0;

    if (Wx_Field(json, "SD", tmp, sizeof(tmp)))
    {
        strncpy(s_wx_humi, tmp, sizeof(s_wx_humi) - 1);
        s_wx_humi[sizeof(s_wx_humi) - 1] = 0;
    }
    else strcpy(s_wx_humi, "--");

    if (Wx_Field(json, "WD", tmp, sizeof(tmp)))
        Wx_TextToGbk(tmp, s_wx_wind, sizeof(s_wx_wind));
    else
        s_wx_wind[0] = 0;

    if (Wx_Field(json, "WS", tmp, sizeof(tmp)))
        Wx_TextToGbk(tmp, s_wx_wlev, sizeof(s_wx_wlev));
    else
        s_wx_wlev[0] = 0;

    s_wx_valid = 1;
    return 1;
}

static void Clock_DrawTime(void)
{
    char buf[16];
    sprintf(buf, "%02d:%02d", (int)s_clk_hour, (int)s_clk_min);
    LCD_ShowStringBig(48, 52, buf, COLOR_WHITE, UI_CARD_BG, 5);
}

void Clock_DrawCard(void)
{
    static const char *const WD[7] = { "\xC8\xD5", "\xD2\xBB", "\xB6\xFE", "\xC8\xFD", "\xCB\xC4", "\xCE\xE5", "\xC1\xF9" };
    char buf[24];

    LCD_FillRound(CLK_X1, CLK_Y1, CLK_X2, CLK_Y2, 12, UI_CARD_BG);
    LCD_DrawRoundRect(CLK_X1, CLK_Y1, CLK_X2, CLK_Y2, 12, UI_CARD_LINE);

    if (!s_clk_valid)
    {
        LCD_ShowStringCN(43, 74, "\xCA\xB1\xBC\xE4\xCD\xAC\xB2\xBD\xD6\xD0", COLOR_DIM, UI_CARD_BG, 2);
        LCD_ShowStringCN(83, 128, "\xC7\xEB\xB5\xC8\xB4\xFD\xC1\xAA\xCD\xF8", COLOR_DIM, UI_CARD_BG, 1);
        return;
    }

    Clock_DrawTime();
    LCD_Fill(48, 100, 197, 101, COLOR_ACCENT);

    sprintf(buf, "%04d-%02d-%02d", (int)s_clk_year, (int)s_clk_mon, (int)s_clk_day);
    LCD_ShowString(63, 112, buf, COLOR_TEXT, UI_CARD_BG, 2);

    sprintf(buf, "\xD0\xC7\xC6\xDA%s", WD[s_clk_wday % 7]);
    LCD_ShowStringCN(99, 140, buf, COLOR_DIM, UI_CARD_BG, 1);

    LCD_ShowStringCN(83, 164, "\xB1\xB1\xBE\xA9\xCA\xB1\xBC\xE4", COLOR_DIM, UI_CARD_BG, 1);
}

void Wx_DrawCard(void)
{
    char buf[32];
    uint16_t tx;

    LCD_FillRound(WX_X1, WX_Y1, WX_X2, WX_Y2, 12, UI_CARD_BG);
    LCD_DrawRoundRect(WX_X1, WX_Y1, WX_X2, WX_Y2, 12, UI_CARD_LINE);
    LCD_ShowStringCN(266, 52, "\xDB\xAF\xB6\xBC \xCA\xB5\xCA\xB1", COLOR_DIM, UI_CARD_BG, 1);


    if (!s_wx_valid)
    {
        LCD_ShowStringCN(275, 92, "\xCC\xEC\xC6\xF8\xBB\xF1\xC8\xA1\xD6\xD0", COLOR_DIM, UI_CARD_BG, 2);
        LCD_ShowStringCN(300, 138, "\xC7\xEB\xB5\xC8\xB4\xFD\xC1\xAA\xCD\xF8", COLOR_DIM, UI_CARD_BG, 1);
        return;
    }

    LCD_ShowStringBig(268, 78, s_wx_temp, 0xFD20, UI_CARD_BG, 4);
    tx = (uint16_t)(268 + (uint16_t)strlen(s_wx_temp) * 24u + 4u);
    LCD_ShowStringCN(tx, 78, "\xA1\xE6", 0xFD20, UI_CARD_BG, 2);

    sprintf(buf, "\xCA\xAA\xB6\xC8 %s", s_wx_humi);
    LCD_ShowStringCN(266, 132, buf, COLOR_TEXT, UI_CARD_BG, 1);

    sprintf(buf, "%s %s", s_wx_wind, s_wx_wlev);
    LCD_ShowStringCN(266, 158, buf, COLOR_TEXT, UI_CARD_BG, 1);
}

static void Clock_AddSecond(void)
{
    static const uint8_t DIM[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    uint8_t dim;
    uint8_t paint = 0;

    s_clk_sec++;
    if (s_clk_sec < 60) return;
    s_clk_sec = 0;
    s_clk_min++;
    if (s_clk_min < 60)
    {
        paint = 1;                       /* minute changed */
    }
    else
    {
        s_clk_min = 0;
        s_clk_hour++;
        if (s_clk_hour < 24)
        {
            paint = 1;
        }
        else
        {
            s_clk_hour = 0;
            dim = DIM[(s_clk_mon - 1) % 12];
            if (s_clk_mon == 2 &&
                ((s_clk_year % 4 == 0 && s_clk_year % 100 != 0) || s_clk_year % 400 == 0))
                dim = 29;
            s_clk_day++;
            if (s_clk_day > dim)
            {
                s_clk_day = 1;
                s_clk_mon++;
                if (s_clk_mon > 12) { s_clk_mon = 1; s_clk_year++; }
            }
            s_clk_wday = (uint8_t)((s_clk_wday + 1) % 7);
            paint = 2;                   /* date changed */
        }
    }

    /* the clock card only exists on the main screen */
    if (s_state != ST_MENU || s_overlay) return;
    if (paint == 2) Clock_DrawCard();
    else            Clock_DrawTime();
}

void Clock_Weather_Tick(void)
{
    uint32_t now = DWT->CYCCNT;
    uint32_t dcyc = now - s_cyc_last;
    uint8_t  r;

    s_cyc_last = now;
    if (dcyc > (CLK_CYC_PER_MS * 200u)) dcyc = CLK_CYC_PER_MS * 200u;
    s_cyc_acc += dcyc;
    /* one second = 168e6 cycles; the old threshold was one MILLISECOND,
       which made the clock run 1000x too fast */
    while (s_cyc_acc >= CLK_CYC_PER_SEC)
    {
        s_cyc_acc -= CLK_CYC_PER_SEC;
        if (s_clk_valid) Clock_AddSecond();
    }


    /* the title bar only exists on the main screen */
    if (s_state == ST_MENU && !s_overlay) TitleBar_DrawLink();

    r = ESP8266_FetchPoll(s_wx_rx, sizeof(s_wx_rx));

    if (r == 1)
    {
        if (Clock_ParseDate(s_wx_rx)) Wx_Parse(s_wx_rx);
        printf("WX: %04d-%02d-%02d %02d:%02d:%02d temp=%s humi=%s wind=%s %s\r\n",
               (int)s_clk_year, (int)s_clk_mon, (int)s_clk_day,
               (int)s_clk_hour, (int)s_clk_min, (int)s_clk_sec,
               s_wx_temp, s_wx_humi, s_wx_wind, s_wx_wlev);
        s_wx_next = WX_REFRESH_TICKS;
        /* redraw only the two cards: a full Menu_Draw would freeze the UI */
        if (s_state == ST_MENU && !s_overlay)
        {
            Clock_DrawCard();
            Wx_DrawCard();
        }
        return;
    }
    if (r == 2)
    {
        /* nothing on screen changed, so do not repaint at all */
        printf("WX: fetch failed, retry in %us\r\n", (unsigned)(WX_RETRY_TICKS / 200u));
        s_wx_next = WX_RETRY_TICKS;
        return;
    }


    if (s_wx_next) s_wx_next--;
    if (!s_wx_next && !ESP8266_FetchBusy() && !s_chat_mode)
        s_wx_next = (ESP8266_FetchStart(WX_HOST, WX_PATH) == 0)
                        ? WX_REFRESH_TICKS : WX_RETRY_TICKS;
}
