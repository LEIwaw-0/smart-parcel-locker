/**
  ******************************************************************************
  * @file    ld3320.c
  * @brief   UNV-LD3320+STC11 module UART driver (STM32 side)
  *
  *          Module TXD -> PA3 (USART2_RX), module RXD -> PA2 (unused)
  *          9600 8N1. The STC11 firmware sends ASCII lines:
  *            "LD3320 READY"   boot beacon
  *            "LD_OK R06=XX"   parallel bus selftest passed
  *            "LD_ERR"         bus selftest failed
  *            "CMD:XX"         legacy recognition result (hex)
  *            "CODE:DDDDDD"    6-digit pickup code (decimal)
  *          USART2 ISR collects each line; both CMD and CODE lines are
  *          kept in separate slots so the main loop can poll either.
  ******************************************************************************
  */
#include "ld3320.h"
#include <string.h>

#define LD_LINE_LEN 40

static volatile char    line_buf[LD_LINE_LEN];
static volatile uint8_t line_pos  = 0;

/* Legacy CMD slot */
static volatile uint8_t cmd_ready = 0;
static volatile uint8_t cmd_value = 0;

/* 6-digit CODE slot (confirmed) */
static volatile uint8_t code_ready = 0;
static volatile char     code_buf[7];

/* Pending CODE slot (awaiting dui/cuo confirmation) */
static volatile uint8_t pcode_ready = 0;
static volatile char     pcode_buf[7];

/* RETRY slot (user answered "cuo" at the final confirm) */
static volatile uint8_t retry_ready = 0;

/* Per-digit progress slot ("DIGIT:n VAL:x") */
static volatile uint8_t digit_ready = 0;
static volatile uint8_t digit_idx   = 0;
static volatile uint8_t digit_val   = 0;

/* UNDO slot (user said "cuo" while entering digits) */
static volatile uint8_t undo_ready = 0;

/* CHAT slots: chat wake word "xiao zhi" + recognized chat keyword code */
static volatile uint8_t chatwake_ready = 0;
static volatile uint8_t chat_ready = 0;
static volatile uint8_t chat_code = 0;

/* Diagnostic slot: EVERY complete line received from the module is copied
   here (raw, unparsed), so the main loop can echo it on USART1. This lets
   us verify the wiring/firmware without a logic analyser. */
static volatile uint8_t raw_ready = 0;
static volatile char    raw_line[LD_LINE_LEN];

static int hex2nib(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

/* ===================== USART2 init: PA2(TX)/PA3(RX) 9600 ===================== */
void LD3320_Init(void)
{
    GPIO_InitTypeDef  GPIO_InitStructure;
    USART_InitTypeDef USART_InitStructure;
    NVIC_InitTypeDef  NVIC_InitStructure;

    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2, ENABLE);
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);

    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_2 | GPIO_Pin_3;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF;
    GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
    GPIO_InitStructure.GPIO_Speed  = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_PuPd  = GPIO_PuPd_UP;   /* module TX idle high */
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    GPIO_PinAFConfig(GPIOA, GPIO_PinSource2, GPIO_AF_USART2);
    GPIO_PinAFConfig(GPIOA, GPIO_PinSource3, GPIO_AF_USART2);

    USART_InitStructure.USART_BaudRate            = 9600;
    USART_InitStructure.USART_WordLength          = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits            = USART_StopBits_1;
    USART_InitStructure.USART_Parity              = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode                = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(USART2, &USART_InitStructure);

    USART_ITConfig(USART2, USART_IT_RXNE, ENABLE);

    NVIC_InitStructure.NVIC_IRQChannel                   = USART2_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 2;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority        = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    USART_Cmd(USART2, ENABLE);
}

/* ===================== line parser ISR ===================== */
/* Parse a completed line (in line_buf, length line_pos). */
static void parse_line(void)
{
    /* "CMD:XX" */
    if (line_pos >= 6 &&
        line_buf[0] == 'C' && line_buf[1] == 'M' &&
        line_buf[2] == 'D' && line_buf[3] == ':')
    {
        int hi = hex2nib(line_buf[4]);
        int lo = hex2nib(line_buf[5]);
        if (hi >= 0 && lo >= 0)
        {
            cmd_value = (uint8_t)((hi << 4) | lo);
            cmd_ready = 1;
        }
        return;
    }

    /* "CODE:DDDDDD" - 6 decimal digits (confirmed by dui) */
    if (line_pos >= 11 &&
        line_buf[0] == 'C' && line_buf[1] == 'O' &&
        line_buf[2] == 'D' && line_buf[3] == 'E' &&
        line_buf[4] == ':')
    {
        uint8_t i;
        for (i = 0; i < 6; i++)
        {
            char c = line_buf[5 + i];
            if (c < '0' || c > '9') return;   /* malformed */
            code_buf[i] = c;
        }
        code_buf[6] = 0;
        code_ready = 1;
        return;
    }

    /* "PCODE:DDDDDD" - pending code, read it back and ask dui/cuo */
    if (line_pos >= 12 &&
        line_buf[0] == 'P' && line_buf[1] == 'C' &&
        line_buf[2] == 'O' && line_buf[3] == 'D' &&
        line_buf[4] == 'E' && line_buf[5] == ':')
    {
        uint8_t i;
        for (i = 0; i < 6; i++)
        {
            char c = line_buf[6 + i];
            if (c < '0' || c > '9') return;   /* malformed */
            pcode_buf[i] = c;
        }
        pcode_buf[6] = 0;
        pcode_ready = 1;
        return;
    }

    /* "RETRY" - user said cuo at final confirm, restart digit entry */
    if (line_pos >= 5 &&
        line_buf[0] == 'R' && line_buf[1] == 'E' &&
        line_buf[2] == 'T' && line_buf[3] == 'R' &&
        line_buf[4] == 'Y')
    {
        retry_ready = 1;
        return;
    }

    /* "UNDO" - user said cuo during capture, erase last digit */
    if (line_pos >= 4 &&
        line_buf[0] == 'U' && line_buf[1] == 'N' &&
        line_buf[2] == 'D' && line_buf[3] == 'O')
    {
        undo_ready = 1;
        return;
    }

    /* "DIGIT:n VAL:x" - per-digit capture progress
       indices: 0..5 "DIGIT:", 6=idx, 7=' ', 8..10 "VAL", 11=':', 12=val */
    if (line_pos >= 13 &&
        line_buf[0] == 'D' && line_buf[1] == 'I' &&
        line_buf[2] == 'G' && line_buf[3] == 'I' &&
        line_buf[4] == 'T' && line_buf[5] == ':' &&
        line_buf[6] >= '0' && line_buf[6] <= '9' &&
        line_buf[8] == 'V' && line_buf[9] == 'A' &&
        line_buf[10] == 'L' && line_buf[11] == ':')
    {
        char vi = line_buf[6];
        char vv = line_buf[12];
        if (vv >= '0' && vv <= '9')
        {
            digit_idx  = (uint8_t)(vi - '0');
            digit_val  = (uint8_t)(vv - '0');
            digit_ready = 1;
        }
        return;
    }

    /* "CHAT:WAKE" - chat wake word "xiao zhi" heard */
    if (line_pos >= 9 &&
        line_buf[0] == 'C' && line_buf[1] == 'H' &&
        line_buf[2] == 'A' && line_buf[3] == 'T' &&
        line_buf[4] == ':' &&
        line_buf[5] == 'W' && line_buf[6] == 'A' &&
        line_buf[7] == 'K' && line_buf[8] == 'E')
    {
        chatwake_ready = 1;
        return;
    }

    /* "CHAT:NN" - chat keyword recognized (NN = hex code 0x40..0x7F) */
    if (line_pos >= 7 &&
        line_buf[0] == 'C' && line_buf[1] == 'H' &&
        line_buf[2] == 'A' && line_buf[3] == 'T' &&
        line_buf[4] == ':')
    {
        int hi = hex2nib(line_buf[5]);
        int lo = hex2nib(line_buf[6]);
        if (hi >= 0 && lo >= 0)
        {
            chat_code = (uint8_t)((hi << 4) | lo);
            chat_ready = 1;
        }
        return;
    }
}

void USART2_IRQHandler(void)
{
    if (USART_GetITStatus(USART2, USART_IT_RXNE) != RESET)
    {
        char ch = (char)USART_ReceiveData(USART2);   /* read DR clears RXNE */

        if (ch == '\r')
            return;

        if (ch == '\n')                              /* end of line: parse */
        {
            uint8_t k;
            line_buf[line_pos] = 0;                  /* NUL-terminate */
            parse_line();
            /* keep a raw copy of any non-empty line for diagnostics */
            if (line_pos > 0)
            {
                for (k = 0; k < line_pos && k < (LD_LINE_LEN - 1); k++)
                    raw_line[k] = line_buf[k];
                raw_line[k] = 0;
                raw_ready = 1;
            }
            line_pos = 0;
        }
        else if (line_pos < LD_LINE_LEN - 1)
        {
            line_buf[line_pos++] = ch;
        }
        else
        {
            line_pos = 0;                            /* overflow: resync */
        }
    }

    if (USART_GetITStatus(USART2, USART_IT_ORE) != RESET)
    {
        (void)USART2->SR;
        (void)USART2->DR;
    }
}

/* ===================== poll for parsed legacy command ===================== */
int LD3320_GetCmd(void)
{
    int v;
    if (!cmd_ready)
        return -1;
    v = cmd_value;
    cmd_ready = 0;
    return v;
}

/* ===================== poll for 6-digit pickup code ===================== */
uint8_t LD3320_GetCode(char out_code[7])
{
    if (!code_ready)
        return 0;
    memcpy(out_code, (const char *)code_buf, 7);
    code_ready = 0;
    return 1;
}

/* ===================== pending code (readback before confirm) ===================== */
uint8_t LD3320_GetPendingCode(char out_code[7])
{
    if (!pcode_ready)
        return 0;
    memcpy(out_code, (const char *)pcode_buf, 7);
    pcode_ready = 0;
    return 1;
}

/* ===================== RETRY event (user said "cuo" at confirm) ===================== */
uint8_t LD3320_GetRetry(void)
{
    if (!retry_ready)
        return 0;
    retry_ready = 0;
    return 1;
}

/* ===================== per-digit progress ===================== */
uint8_t LD3320_GetDigit(uint8_t *idx, uint8_t *val)
{
    if (!digit_ready)
        return 0;
    *idx = digit_idx;
    *val = digit_val;
    digit_ready = 0;
    return 1;
}

/* ===================== UNDO event ("cuo" during capture) ===================== */
uint8_t LD3320_GetUndo(void)
{
    if (!undo_ready)
        return 0;
    undo_ready = 0;
    return 1;
}

/* ===================== CHAT wake word ("xiao zhi") ===================== */
uint8_t LD3320_GetChatWake(void)
{
    if (!chatwake_ready)
        return 0;
    chatwake_ready = 0;
    return 1;
}

/* ===================== CHAT keyword code ===================== */
uint8_t LD3320_GetChat(uint8_t *code)
{
    if (!chat_ready)
        return 0;
    *code = chat_code;
    chat_ready = 0;
    return 1;
}

/* ===================== poll for last RAW line (diagnostic) =====================
 * Returns 1 and copies the last raw UART line into out if a new line arrived
 * since the previous call, else 0. Use to verify LD3320 wiring/firmware. */
uint8_t LD3320_GetRaw(char *out, uint8_t maxlen)
{
    uint8_t k;
    if (!raw_ready || maxlen == 0)
        return 0;
    for (k = 0; k < (uint8_t)(maxlen - 1); k++) {
        char c = raw_line[k];
        out[k] = c;
        if (c == 0) break;
    }
    out[k] = 0;
    raw_ready = 0;
    return 1;
}
