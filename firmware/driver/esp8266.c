/**
  ******************************************************************************
  * @file    esp8266.c
  * @brief   ESP8266 WiFi模块 AT指令驱动 (STM32F407, USART3 PB10/PB11)
  *
  *          非阻塞状态机，由主循环调用 ESP8266_Tick() 驱动。
  *          接收端为中断驱动：普通AT响应按行解析；
  *          "+IPD,<长度>:<数据>" 载荷按长度提取，确保中继服务器的
  *          回复（可能包含\r\n）被完整捕获。
  */
#include "esp8266.h"
#include "usart.h"
#include <string.h>
#include <stdio.h>

/* ======================== 宏定义 ======================== */
#define ESP_USART   USART3              /* 使用的串口 */
#define ESP_LINE_LEN 128                /* 单行最大长度 */
#define ESP_IPD_LEN  2048               /* IPD数据最大长度 */
#define ESP_SEND_LEN 256                /* 发送缓冲区最大长度 */

/* ======================== 连接状态枚举 ======================== */
typedef enum {
    ES_INIT = 0,        /* 发送AT，等待OK                        */
    ES_ATE0,            /* 发送ATE0，关闭回显                     */
    ES_CWMODE,          /* 发送AT+CWMODE=1，设置为Station模式      */
    ES_CWJAP,           /* 发送AT+CWJAP，连接WiFi                  */
    ES_CIPMUX,          /* 发送AT+CIPMUX=0，单连接模式              */
    ES_CIPSENDBUF,      /* 发送AT+CIPSENDBUF=0，禁用发送缓冲       */
    ES_CIPSTART,        /* 发送AT+CIPSTART，建立TCP连接             */
    ES_READY,           /* 链路就绪，空闲状态                      */
    ES_SEND_PROMPT,     /* 已发AT+CIPSEND=n，等待'>'提示符         */
    ES_SEND_DATA,       /* 数据已发送，等待SEND OK                  */
    ES_WAIT_IPD,
    ES_ERROR,           /* 出错1秒后自动重试整个握手               */
    ES_OFFLINE          /* WiFi连接失败，停止重试，保持离线         */
} esp_state_t;

/* ======================== 状态变量 ======================== */
static volatile esp_state_t s_state = ES_INIT;  /* 当前状态 */
static volatile uint32_t    s_tick  = 0;         /* 主循环tick计数器(~5ms/tick) */
static uint8_t              s_init_try = 0;      /* AT握手失败次数 */
static uint8_t              s_cwjap_try = 0;     /* CWJAP连接WiFi失败次数 */
#define MAX_CWJAP_RETRY  5u                       /* 最多重试5次后停止 */
static volatile uint8_t     s_at_ok   = 0;      /* 收到"OK" */
static volatile uint8_t     s_at_err  = 0;      /* 收到"ERROR"或"FAIL" */
static volatile uint8_t     s_busy    = 0;      /* 收到"busy p..." */
static volatile uint8_t     s_at_prompt = 0;    /* 收到'>'提示符 */
static volatile uint8_t     s_send_ok = 0;      /* 收到"SEND OK" */
static volatile uint8_t     s_connected = 0;    /* 收到"CONNECT" */
static uint8_t              s_cipstart_try = 0; /* CIPSTART重试次数 */
static uint8_t              s_esp_sent  = 0;     /* 1=命令已发送，等待响应 */
static uint32_t             s_sent_tick = 0;    /* 命令发送时的tick值 */

/* 响应等待延迟：发送命令后至少等30个tick(~50ms)再检查响应，
   防止ISR在esp_send_cmd内部printf期间设置的残留标志被误判 */
#define ESP_RESP_DELAY  30u

/* CIPSTART前等待时间：首次10秒(CIPSTART_SETTLE)，重试也10秒(CIPSTART_RETRY)
   ESP8266固件v2.2.0在CWJAP后DHCP/NTP任务可能阻塞10-30秒 */
#define CIPSTART_SETTLE 2000u
#define CIPSTART_RETRY  2000u

/* ======================== 串口接收行缓冲 ======================== */
static volatile char    s_line[ESP_LINE_LEN];
static volatile uint8_t s_line_pos = 0;

/* ======================== 调试行队列 ========================
 * printf() 绝对不能在USART3中断服务程序里调用：
 * 115200波特率下一个printf约耗时1.1ms，会拖慢RX移位寄存器，
 * 导致模块快速的"OK"回复被静默丢弃(ORE溢出错误)，
 * 进而使握手死锁在"收到回显但永远等不到OK"的状态。
 * ISR只入队，由Tick()出队打印。*/
#define DBG_QLEN 4

/* ================= 非阻塞 HTTP GET (borrowed-link fetch) ================= */
#define ESP_FETCH_RX_LEN     1024u
#define ESP_FETCH_HOST_LEN     64u
#define ESP_FETCH_PATH_LEN     96u
#define ESP_FETCH_REQ_LEN     224u
#define FETCH_CONNECT_TICKS  2000u   /* 10s : CIPSTART to a remote host   */
#define FETCH_SEND_TICKS      600u   /*  3s : CIPSEND / SEND OK           */
#define FETCH_RECV_TICKS     3000u   /* 15s : whole HTTP reply            */
#define FETCH_QUIET_TICKS     300u   /* 1.5s: no new bytes -> reply done  */
#define FETCH_CLOSE_TICKS     400u   /*  2s : CIPCLOSE                    */

typedef enum { FS_UNLINK = 0, FS_START, FS_SEND, FS_DATA, FS_RECV, FS_CLOSE } fetch_state_t;


static uint8_t  s_fetch_active = 0;
static uint8_t  s_fetch_state  = FS_START;
static uint8_t  s_fetch_result = 0;    /* 0 = running, 1 = ok, 2 = failed */
static uint8_t  s_fetch_unlinked = 0;  /* 1 = AT+CIPCLOSE already issued     */

static char     s_fetch_host[ESP_FETCH_HOST_LEN];
static char     s_fetch_path[ESP_FETCH_PATH_LEN];
static char     s_fetch_req[ESP_FETCH_REQ_LEN];
static char     s_fetch_rx[ESP_FETCH_RX_LEN];
static uint16_t s_fetch_len  = 0;
static uint32_t s_fetch_last = 0;      /* tick of the last received chunk */
static uint32_t s_fetch_start_tick = 0;

/* "+IPD,<len>:" header is parsed byte by byte so the payload never has to
   fit in s_line; s_ipd_got counts every payload byte so an oversized chunk
   cannot desynchronise the stream. */
static volatile uint8_t  s_ipd_hdr = 0;
static volatile uint16_t s_ipd_got = 0;

static char            s_dbgq[DBG_QLEN][ESP_LINE_LEN];
static volatile uint8_t s_dbg_head = 0;
static volatile uint8_t s_dbg_tail = 0;

/* ======================== IPD(接收数据)捕获 ======================== */
static volatile char    s_ipd[ESP_IPD_LEN];
static volatile uint16_t s_ipd_len = 0;        /* 已捕获字节数 */
static volatile uint16_t s_ipd_want = 0;       /* 期望载荷长度 */
static volatile uint8_t  s_ipd_mode = 0;       /* 1=正在读取载荷字节 */
static volatile uint8_t  s_ipd_ready = 0;      /* 1=完整载荷已就绪 */

/* ======================== 发送缓冲 ======================== */
static char     s_sendbuf[ESP_SEND_LEN];
static uint16_t s_sendlen = 0;
static volatile uint8_t s_send_pending = 0;

/* ============================================================
 *  底层发送函数
 * ============================================================ */

/* 发送单个字符，阻塞等待发送完成标志 */
static void esp_putc(char c)
{
    while (USART_GetFlagStatus(ESP_USART, USART_FLAG_TC) == RESET)
        ;
    USART_SendData(ESP_USART, (uint8_t)c);
}

/* 发送字符串 */
static void esp_send(const char *s)
{
    while (*s) esp_putc(*s++);
}

/* 发送AT命令（先打印调试信息，再发命令+CRLF） */
static void esp_send_cmd(const char *cmd)
{
    printf("[ESP] TX: %s\r\n", cmd);
    esp_send(cmd);
    esp_send("\r\n");
}

/* ============================================================
 *  USART3初始化 PB10(TX) / PB11(RX), 115200 8N1
 * ============================================================ */
void ESP8266_Init(void)
{
    GPIO_InitTypeDef  GPIO_InitStructure;
    USART_InitTypeDef USART_InitStructure;
    NVIC_InitTypeDef  NVIC_InitStructure;

    /* 开启USART3和GPIOB时钟 */
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART3, ENABLE);
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOB, ENABLE);

    /* PB10/PB11 复用推挽50MHz，上拉 */
    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_10 | GPIO_Pin_11;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF;
    GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_PuPd  = GPIO_PuPd_UP;
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    /* 映射PB10/PB11到USART3 */
    GPIO_PinAFConfig(GPIOB, GPIO_PinSource10, GPIO_AF_USART3);
    GPIO_PinAFConfig(GPIOB, GPIO_PinSource11, GPIO_AF_USART3);

    /* 串口参数: 115200, 8位数据, 1停止位, 无校验, 无流控, 收发模式 */
    USART_InitStructure.USART_BaudRate            = 115200;
    USART_InitStructure.USART_WordLength          = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits            = USART_StopBits_1;
    USART_InitStructure.USART_Parity              = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode                = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(ESP_USART, &USART_InitStructure);

    /* 开启接收非空中断 */
    USART_ITConfig(ESP_USART, USART_IT_RXNE, ENABLE);

    /* 配置USART3中断：抢占优先级6、子优先级0
       FreeRTOS 接入后 NVIC 分组为 Group_4（4 位全抢占），此处数值即抢占优先级，
       必须 >= configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY(5)，否则在中断里调用
       FromISR 版本内核 API 会触发 configASSERT。三个串口中断统一使用 6。 */
    NVIC_InitStructure.NVIC_IRQChannel                   = USART3_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 6;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority        = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    /* 使能串口 */
    USART_Cmd(ESP_USART, ENABLE);

    /* 复位状态机所有变量 */
    s_state        = ES_INIT;
    s_tick         = 0;
    s_init_try     = 0;
    s_dbg_head     = 0;
    s_dbg_tail     = 0;
    s_at_ok        = 0;
    s_at_err       = 0;
    s_busy         = 0;
    s_at_prompt    = 0;
    s_send_ok      = 0;
    s_connected    = 0;
    s_cipstart_try = 0;
    s_esp_sent     = 0;
    s_sent_tick    = 0;
    s_send_pending = 0;
    s_sendlen      = 0;
    s_ipd_ready   = 0;
    s_ipd_mode    = 0;
    printf("[ESP] init USART3 PB10/PB11 115200\r\n");
}

/* ============================================================
 *  AT响应行解析器（从中断服务程序调用）
 *
 *  注意：此函数在中断上下文中执行！
 *  - 只设置标志位，不调用printf
 *  - 不修改s_state（防止ISR干扰主循环状态机）
 *  - 不修改s_esp_sent
 *  CLOSED只清s_connected，不触发状态跳转
 * ============================================================ */
static void parse_line(void)
{
    char buf[ESP_LINE_LEN];
    uint8_t i;
    uint8_t h    = s_dbg_head;
    uint8_t next = (uint8_t)((h + 1) % DBG_QLEN);

    /* 从volatile行缓冲拷贝到本地buf */
    for (i = 0; i < s_line_pos && i < (ESP_LINE_LEN - 1); i++)
        buf[i] = s_line[i];
    buf[i] = 0;

    if (buf[0] == 0) return;

    /* 按内容设置对应标志位 */
    if      (strcmp(buf, "OK") == 0)             s_at_ok = 1;
    else if (strcmp(buf, "ERROR") == 0)           s_at_err = 1;
    else if (strcmp(buf, "FAIL") == 0)            s_at_err = 1;
    else if (strcmp(buf, "SEND OK") == 0)         s_send_ok = 1;
    else if (strcmp(buf, "CONNECT") == 0)         s_connected = 1;
    else if (strcmp(buf, "ALREADY CONNECTED") == 0) { s_connected = 1; s_at_ok = 1; }
    else if (strncmp(buf, "busy p", 6) == 0)     s_busy = 1;
    else if (strncmp(buf, "+CWJAP:", 7) == 0)    s_at_err = 1;  /* WiFi连接失败，带原因码 */
    else if (strcmp(buf, "WIFI CONNECTED") == 0)  { /* 继续等GOT IP */ }
    else if (strcmp(buf, "WIFI GOT IP") == 0)     s_at_ok = 1;
    else if (strcmp(buf, "CLOSED") == 0)          { s_connected = 0; s_state = ES_CIPSTART; s_esp_sent = 0; s_tick = 0; }
    /* CIPSEND的">"提示符可能带尾随空格 */
    else if (buf[0] == '>' && buf[1] == 0)        s_at_prompt = 1;
    /* "+IPD,<len>:" 载荷头在中断字节循环中处理 */

    /* 入队调试打印 - ISR里绝不调用printf */
    if (next != s_dbg_tail)
    {
        strcpy(s_dbgq[h], buf);
        s_dbg_head = next;
    }
}

/* ============================================================
 *  USART3 接收中断服务程序
 *
 *  逐字节接收，按行组装，遇到\n时调用parse_line()；
 *  遇到"+IPD,<len>:"时切换到载荷捕获模式。
 * ============================================================ */
void USART3_IRQHandler(void)
{
    if (USART_GetITStatus(ESP_USART, USART_IT_RXNE) != RESET)
    {
        char ch = (char)USART_ReceiveData(ESP_USART);

        if (s_ipd_hdr)
        {
            /* ---- "+IPD,<len>:" header: digits then ':' ---- */
            if (ch == ':')
            {
                s_ipd_hdr      = 0;
                s_ipd_len      = 0;
                s_ipd_got      = 0;
                s_ipd[s_ipd_len] = 0;
                if (s_ipd_want > 0) s_ipd_mode = 1;
            }
            else if (ch >= '0' && ch <= '9')
            {
                s_ipd_want = (uint16_t)(s_ipd_want * 10 + (uint16_t)(ch - '0'));
            }
        }
        else if (s_ipd_mode)

        {
            /* ---- 载荷字节模式：逐字节捕获直到达到期望长度 ---- */
            if (s_ipd_len < s_ipd_want && s_ipd_len < (ESP_IPD_LEN - 1))
            {
                s_ipd[s_ipd_len++] = ch;
                s_ipd_got++;
                if (s_ipd_got >= s_ipd_want)
                {
                    s_ipd[s_ipd_len] = 0;
                    s_ipd_mode  = 0;
                    s_ipd_ready = 1;
                }
            }

            else
            {
                /* 溢出保护：停止捕获 */
                s_ipd[s_ipd_len] = 0;
                s_ipd_mode  = 0;
                s_ipd_ready = 1;
            }
        }
        else
        {
            /* ---- 普通行模式 ---- */
            if (ch == '\r') { /* 忽略\r */ }
            else if (ch == '\n')
            {
                /* 换行：一行完整，交给parse_line */
                s_line[s_line_pos] = 0;
                if (s_line_pos > 0)
                {
                    /* 检查是否为 "+IPD,<len>:" 行 */
                    if (s_line_pos >= 6 &&
                        s_line[0] == '+' && s_line[1] == 'I' &&
                        s_line[2] == 'P' && s_line[3] == 'D' &&
                        s_line[4] == ',')
                    {
                        /* 查找冒号分隔符 */
                        uint8_t k;
                        uint16_t len = 0;
                        for (k = 5; k < s_line_pos; k++)
                        {
                            if (s_line[k] == ':') break;
                            if (s_line[k] >= '0' && s_line[k] <= '9')
                                len = len * 10 + (uint16_t)(s_line[k] - '0');
                        }
                        if (len > 0 && len < ESP_IPD_LEN)
                        {
                            uint8_t i;
                            uint8_t pstart = (uint8_t)(k + 1);
                            uint8_t in_line = (uint8_t)(s_line_pos > pstart ? s_line_pos - pstart : 0);
                            s_ipd_want = len;
                            s_ipd_len  = 0;
                            /* +IPD,<len>:<data> 的 <data> 紧跟冒号在同一行，
                               必须先把冒号后的字节拷入 s_ipd，否则会被
                               随后的 s_line_pos=0 清空丢弃 */
                            for (i = 0; i < in_line && i < len; i++)
                                s_ipd[i] = s_line[pstart + i];
                            s_ipd_len = i;
                            s_ipd[s_ipd_len] = 0;
                            if (s_ipd_len >= len)
                            {
                                s_ipd_mode  = 0;
                                s_ipd_ready = 1;
                            }
                            else
                            {
                                s_ipd_mode = 1;
                            }
                        }
                    }
                    else
                    {
                        parse_line();
                    }
                }
                s_line_pos = 0;
            }
            else if (ch == '>' && s_line_pos == 0)
            {
                /* CIPSEND的'>'提示符不带换行 */
                s_at_prompt = 1;
            }
            else if (s_line_pos < ESP_LINE_LEN - 1)
            {
                s_line[s_line_pos++] = ch;
                /* divert "+IPD," into the dedicated header parser so a large
                   payload can never overflow the AT line buffer */
                if (s_line_pos == 5 && s_line[0] == '+' && s_line[1] == 'I' &&
                    s_line[2] == 'P' && s_line[3] == 'D' && s_line[4] == ',')
                {
                    s_line_pos = 0;
                    s_ipd_hdr  = 1;
                    s_ipd_want = 0;
                }
            }

            else
            {
                s_line_pos = 0;   /* 溢出同步 */
            }
        }
    }

    /* 清除溢出错误标志(ORE) */
    if (USART_GetITStatus(ESP_USART, USART_IT_ORE) != RESET)
    {
        (void)ESP_USART->SR;
        (void)ESP_USART->DR;
    }
}

/* ============================================================
 *  状态机主循环（从main loop调用，每次约5ms）
 *
 *  统一模式：每个状态用 s_esp_sent 防双发：
 *    - s_esp_sent=0 时：清标志→发命令→设s_esp_sent=1
 *    - s_esp_sent=1 且超过ESP_RESP_DELAY后：检查响应
 *    - 收到OK/ERROR或超时后：清s_esp_sent=0，切状态
 *
 *  ISR 不碰 s_esp_sent 与 s_state，所以不可能双发。
 * ============================================================ */
/* ============================================================
 *  Borrowed-link HTTP GET state machine
 *
 *  FS_UNLINK: AT+CIPCLOSE  - CIPMUX=0 allows only one socket, so the relay
 *             link has to go before we can dial the weather host
 *  FS_START : AT+CIPSTART="TCP","<host>",80

 *  FS_SEND  : AT+CIPSEND=<n>, wait for '>'
 *  FS_DATA  : push the raw request, wait for "SEND OK"
 *  FS_RECV  : collect +IPD chunks until quiet / timeout
 *  FS_CLOSE : AT+CIPCLOSE, then hand the link back to the main FSM
 * ============================================================ */
static void esp_fetch_finish(uint8_t result)
{
    s_fetch_result = result;
    s_fetch_active = 0;
    /* hand the link back: rebuild the relay TCP connection from scratch */
    s_connected = 0;
    s_at_ok     = 0;
    s_at_err    = 0;
    s_esp_sent  = 0;
    s_tick      = 0;
    s_state     = ES_CIPSTART;
}

static void esp_fetch_tick(void)
{
    switch (s_fetch_state)
    {
    /* ---------- drop the relay link first ----------
       AT+CIPMUX=0 allows only ONE socket, so the relay link must be torn
       down before dialling the weather host; otherwise AT+CIPSTART just
       answers ERROR and the hand-back below can never reconnect. */
    case FS_UNLINK:
        if (!s_fetch_unlinked)
        {
            s_at_ok = 0; s_at_err = 0; s_connected = 0;
            esp_send_cmd("AT+CIPCLOSE");
            s_fetch_unlinked = 1;
            s_sent_tick      = s_tick;
        }
        /* the module answers "CLOSED" then "OK"; also accept ERROR/timeout */
        else if (s_at_ok || s_at_err || (s_tick - s_sent_tick) > FETCH_CLOSE_TICKS)
        {
            s_at_ok       = 0;
            s_at_err      = 0;
            s_esp_sent    = 0;
            s_fetch_state = FS_START;
        }
        break;

    /* ---------- open the short-lived TCP link ---------- */
    case FS_START:

        if (!s_esp_sent)
        {
            char cmd[ESP_FETCH_HOST_LEN + 32];
            s_at_ok = 0; s_at_err = 0; s_connected = 0;
            snprintf(cmd, sizeof(cmd), "AT+CIPSTART=\"TCP\",\"%s\",80", s_fetch_host);
            esp_send_cmd(cmd);
            s_esp_sent         = 1;
            s_sent_tick        = s_tick;
            s_fetch_start_tick = s_tick;
        }
        else if (s_connected || s_at_ok)
        {
            s_esp_sent    = 0;
            s_fetch_state = FS_SEND;
        }
        else if (s_tick - s_sent_tick > FETCH_CONNECT_TICKS)
        {
            printf("[ESP] fetch: CIPSTART timeout\r\n");
            esp_fetch_finish(2);
        }
        break;

    /* ---------- announce the request length ---------- */
    case FS_SEND:
        if (!s_esp_sent)
        {
            char cmd[24];
            s_fetch_len = (uint16_t)snprintf(s_fetch_req, ESP_FETCH_REQ_LEN,
                "GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: stm32\r\n"
                "Referer: http://www.weather.com.cn/\r\n"
                "Accept: */*\r\nConnection: close\r\n\r\n",

                s_fetch_path, s_fetch_host);
            snprintf(cmd, sizeof(cmd), "AT+CIPSEND=%u", (unsigned)s_fetch_len);
            s_at_prompt = 0; s_send_ok = 0; s_at_err = 0;
            esp_send_cmd(cmd);
            s_esp_sent  = 1;
            s_sent_tick = s_tick;
        }
        else if (s_at_prompt)
        {
            s_at_prompt   = 0;
            s_esp_sent    = 0;
            s_fetch_state = FS_DATA;
        }
        else if (s_at_err || s_tick - s_sent_tick > FETCH_SEND_TICKS)
        {
            printf("[ESP] fetch: CIPSEND rejected\r\n");
            s_fetch_len   = 0;
            s_esp_sent    = 0;
            s_fetch_state = FS_CLOSE;   /* always CIPCLOSE before hand-back */

        }
        break;

    /* ---------- push the request body (raw, no CRLF) ---------- */
    case FS_DATA:
        if (!s_esp_sent)
        {
            uint16_t i;
            for (i = 0; i < s_fetch_len; i++) esp_putc(s_fetch_req[i]);
            s_send_ok   = 0;
            s_esp_sent  = 1;
            s_sent_tick = s_tick;
        }
        else if (s_send_ok)
        {
            s_send_ok     = 0;
            s_esp_sent    = 0;
            s_fetch_len   = 0;
            s_fetch_rx[0] = 0;
            s_fetch_last  = s_tick;
            s_fetch_state = FS_RECV;
        }
        else if (s_tick - s_sent_tick > FETCH_SEND_TICKS)
        {
            printf("[ESP] fetch: no SEND OK\r\n");
            s_fetch_len   = 0;
            s_esp_sent    = 0;
            s_fetch_state = FS_CLOSE;   /* always CIPCLOSE before hand-back */

        }
        break;

    /* ---------- collect the reply ---------- */
    case FS_RECV:
        if (s_ipd_ready)
        {
            uint16_t n = s_ipd_len;
            uint16_t i;
            s_ipd_ready = 0;
            s_ipd_len   = 0;
            for (i = 0; i < n && (uint32_t)(s_fetch_len + i) < (ESP_FETCH_RX_LEN - 1); i++)
                s_fetch_rx[s_fetch_len + i] = s_ipd[i];
            s_fetch_len = (uint16_t)(s_fetch_len + i);
            s_fetch_rx[s_fetch_len] = 0;
            s_fetch_last = s_tick;
        }
        if ((s_fetch_len > 0 && (s_tick - s_fetch_last) > FETCH_QUIET_TICKS) ||
            (s_tick - s_fetch_start_tick) > FETCH_RECV_TICKS)
        {
            s_esp_sent    = 0;
            s_at_ok       = 0;
            s_fetch_state = FS_CLOSE;
        }
        break;

    /* ---------- close and hand the link back ---------- */
    case FS_CLOSE:
        if (!s_esp_sent)
        {
            s_at_ok = 0; s_at_err = 0;
            esp_send_cmd("AT+CIPCLOSE");
            s_esp_sent  = 1;
            s_sent_tick = s_tick;
        }
        else if (s_at_ok || s_at_err || (s_tick - s_sent_tick) > FETCH_CLOSE_TICKS)
        {
            printf("[ESP] fetch: done, %u bytes\r\n", (unsigned)s_fetch_len);
            esp_fetch_finish((s_fetch_len > 0) ? 1 : 2);
        }
        break;

    default:
        esp_fetch_finish(2);
        break;
    }
}

void ESP8266_Tick(void)

{
    /* 先输出ISR入队的调试行（ISR里不能printf） */
    while (s_dbg_tail != s_dbg_head)
    {
        printf("[ESP] RX: %s\r\n", s_dbgq[s_dbg_tail]);
        s_dbg_tail = (uint8_t)((s_dbg_tail + 1) % DBG_QLEN);
    }

    s_tick++;

    /* a fetch owns the link: run its own state machine and skip the rest */
    if (s_fetch_active)
    {
        esp_fetch_tick();
        return;
    }

    switch (s_state)

    {
    /* ---- 状态1: AT握手 ---- */
    case ES_INIT:
        if (!s_esp_sent)
        {
            s_at_ok = 0; s_at_err = 0; s_busy = 0;
            esp_send_cmd("AT");
            s_esp_sent = 1; s_sent_tick = s_tick;
        }
        /* 发送后等待ESP_RESP_DELAY个tick再检查响应 */
        if (s_esp_sent && (s_tick - s_sent_tick) > ESP_RESP_DELAY)
        {
            if (s_at_ok)
            {
                /* 收到OK，进入下一状态 */
                s_at_ok = 0; s_tick = 0; s_init_try = 0; s_esp_sent = 0;
                s_state = ES_ATE0;
            }
            else if (s_tick > 200)
            {
                /* 超时1秒，重试 */
                s_tick = 0; s_esp_sent = 0;
                if (++s_init_try >= 3)
                {
                    s_state = ES_ERROR;
                    s_tick  = 0;
                    printf("[ESP] echo but no OK after 3 tries\r\n");
                    printf("[ESP] check: 1) module 3.3V supply >=300mA  2) wiring"
                           " PB10->RXD, PB11<-TXD (cross!)  3) AT firmware present"
                           " 4) 115200 baud\r\n");
                }
            }
        }
        break;

    /* ---- 状态2: 关闭回显 ---- */
    case ES_ATE0:
        if (!s_esp_sent)
        {
            s_at_ok = 0; s_at_err = 0; s_busy = 0;
            esp_send_cmd("ATE0");
            s_esp_sent = 1; s_sent_tick = s_tick;
        }
        if (s_esp_sent && (s_tick - s_sent_tick) > ESP_RESP_DELAY)
        {
            if (s_at_ok)       { s_at_ok = 0; s_tick = 0; s_esp_sent = 0; s_state = ES_CWMODE; }
            else if (s_tick > 200) { s_tick = 0; s_esp_sent = 0; }
        }
        break;

    /* ---- 状态3: 设置Station模式 ---- */
    case ES_CWMODE:
        if (!s_esp_sent)
        {
            s_at_ok = 0; s_at_err = 0; s_busy = 0;
            esp_send_cmd("AT+CWMODE=1");
            s_esp_sent = 1; s_sent_tick = s_tick;
        }
        if (s_esp_sent && (s_tick - s_sent_tick) > ESP_RESP_DELAY)
        {
            if (s_at_ok)       { s_at_ok = 0; s_tick = 0; s_esp_sent = 0; s_state = ES_CWJAP; }
            else if (s_tick > 200) { s_esp_sent = 0; s_state = ES_ERROR; }
        }
        break;

    /* ---- 状态: 连接WiFi ---- */
    case ES_CWJAP:
        if (!s_esp_sent)
        {
            char cmd[96];
            s_at_ok = 0; s_at_err = 0; s_busy = 0;
            snprintf(cmd, sizeof(cmd), "AT+CWJAP=\"%s\",\"%s\"", ESP_SSID, ESP_PASS);
            esp_send_cmd(cmd);
            s_esp_sent = 1; s_sent_tick = s_tick;
        }
        if (s_esp_sent && (s_tick - s_sent_tick) > ESP_RESP_DELAY)
        {
            if (s_at_ok)
            {
                /* WiFi连上并获取到IP */
                s_at_ok = 0; s_tick = 0; s_esp_sent = 0;
                s_cwjap_try = 0;
                s_cipstart_try = 0;
                s_state = ES_CIPMUX;
            }
            else if (s_at_err)
            {
                /* WiFi连接失败 */
                s_at_err = 0; s_esp_sent = 0;
                if (++s_cwjap_try >= MAX_CWJAP_RETRY)
                {
                    printf("[ESP] WiFi connect failed after %d retries, giving up\r\n", s_cwjap_try);
                    s_state = ES_OFFLINE;
                }
                else
                {
                    printf("[ESP] CWJAP fail retry %d/%d\r\n", s_cwjap_try, MAX_CWJAP_RETRY);
                    s_state = ES_ERROR;
                }
            }
            else if (s_tick > 4000) { s_esp_sent = 0; s_state = ES_ERROR; } /* 20秒超时 */
        }
        break;

    /* ---- 状态5: 单连接模式 ---- */
    case ES_CIPMUX:
        if (!s_esp_sent)
        {
            s_at_ok = 0; s_at_err = 0; s_busy = 0;
            esp_send_cmd("AT+CIPMUX=0");
            s_esp_sent = 1; s_sent_tick = s_tick;
        }
        if (s_esp_sent && (s_tick - s_sent_tick) > ESP_RESP_DELAY)
        {
            if (s_at_ok || s_at_err)
            {
                /* OK或ERROR都继续（部分固件不支持CIPMUX） */
                s_at_ok = 0; s_at_err = 0; s_tick = 0; s_esp_sent = 0;
                s_state = ES_CIPSENDBUF;
            }
            else if (s_tick > 200) { s_esp_sent = 0; s_tick = 0; }
        }
        break;

    /* ---- 状态6: 禁用发送缓冲 ---- */
    /* ESP8266固件v2.2.0的发送缓冲有内存泄漏bug，
       禁用可释放内存，避免后续CIPSTART收到"busy p..." */
    case ES_CIPSENDBUF:
        if (!s_esp_sent)
        {
            s_at_ok = 0; s_at_err = 0; s_busy = 0;
            esp_send_cmd("AT+CIPSENDBUF=0");
            s_esp_sent = 1; s_sent_tick = s_tick;
        }
        if (s_esp_sent && (s_tick - s_sent_tick) > ESP_RESP_DELAY)
        {
            if (s_at_ok || s_at_err)
            {
                /* OK=支持，ERROR=不支持，都忽略继续 */
                s_at_ok = 0; s_at_err = 0; s_tick = 0; s_esp_sent = 0;
                s_state = ES_CIPSTART;
            }
            else if (s_tick > 200) { s_esp_sent = 0; s_tick = 0; }
        }
        break;

    /* ---- 状态7: 建立TCP连接 ---- */
    /* 首次进入时s_tick已被前一状态清零，需要等待CIPSTART_SETTLE(10秒)
       让模块内部DHCP/NTP任务完成，否则会收到"busy p..."。
       重试时等待CIPSTART_RETRY(10秒)。
       使用与所有其他状态完全相同的 s_esp_sent 防双发模式。 */
    case ES_CIPSTART:
        if (!s_esp_sent)
        {
            /* 等待settle时间到达后再发命令 */
            uint32_t settle = (s_cipstart_try == 0) ? CIPSTART_SETTLE : CIPSTART_RETRY;
            if (s_tick >= settle)
            {
                char cmd[80];
                s_at_ok = 0; s_at_err = 0; s_busy = 0; s_connected = 0;
                snprintf(cmd, sizeof(cmd), "AT+CIPSTART=\"TCP\",\"%s\",%d",
                         ESP_TCP_IP, ESP_TCP_PORT);
                esp_send_cmd(cmd);
                s_esp_sent = 1; s_sent_tick = s_tick;
            }
        }
        if (s_esp_sent && (s_tick - s_sent_tick) > ESP_RESP_DELAY)
        {
            if (s_at_ok || s_connected)
            {
                /* TCP连接成功 */
                s_at_ok = 0; s_tick = 0; s_cipstart_try = 0;
                s_esp_sent = 0;
                s_state = ES_READY;
                printf("[ESP] TCP link up, ready\r\n");
            }
            else if (s_at_err)
            {
                /* ERROR：可能是目标服务器未运行 */
                printf("[ESP] CIPSTART ERROR, retry try=%d\r\n", s_cipstart_try + 1);
                s_at_err = 0; s_busy = 0; s_tick = 0; s_esp_sent = 0;
                if (++s_cipstart_try >= 5)
                {
                    s_cipstart_try = 0;
                    s_state = ES_ERROR;
                }
            }
            else if (s_busy)
            {
                /* "busy p..."：模块内部忙，等待后重试 */
                printf("[ESP] CIPSTART busy, retry try=%d\r\n", s_cipstart_try + 1);
                s_busy = 0; s_tick = 0; s_esp_sent = 0;
                if (++s_cipstart_try >= 8)
                {
                    s_cipstart_try = 0;
                    s_state = ES_ERROR;
                }
            }
            else if (s_tick - s_sent_tick > 5000)
            {
                /* 7.5秒无响应，超时重试 */
                printf("[ESP] CIPSTART timeout, retry\r\n");
                s_tick = 0; s_esp_sent = 0;
            }
        }
        break;

    /* ---- 状态8: 链路就绪，等待发送请求 ---- */
    case ES_READY:
        if (!s_connected) { s_state = ES_CIPSTART; break; }
        if (s_send_pending && s_sendlen > 0)
        {
            char cmd[16];
            snprintf(cmd, sizeof(cmd), "AT+CIPSEND=%u", s_sendlen);
            esp_send_cmd(cmd);
            s_state = ES_SEND_PROMPT;
            s_tick  = 0;
        }
        break;

    /* ---- 状态9: 等待'>'提示符 ---- */
    case ES_SEND_PROMPT:
        if (s_at_prompt)
        {
            s_at_prompt = 0;
            esp_send(s_sendbuf);
            s_send_pending = 0;
            s_state = ES_SEND_DATA;
            s_tick  = 0;
        }
        else if (s_tick > 200) { s_state = ES_READY; }   /* 1秒超时放弃 */
        break;

    /* ---- 状态10: 等待SEND OK ---- */
    /* 发送完成后直接回到 ES_READY，不再进入 ES_WAIT_IPD 等待回复。
       原先的 5 秒等待是为 AI 对话回复设计的，对话已改为本地回复，
       且云控指令（OPEN/SET）是云端主动下发的非请求式 IPD，
       由主循环每 tick 调用 ESP8266_GetResponse 轮询取出即可，
       避免 outbound 上报被阻塞 5 秒。 */
    case ES_SEND_DATA:
        if (s_send_ok) { s_send_ok = 0; s_state = ES_READY; s_tick = 0; }
        else if (s_tick > 400) { s_state = ES_READY; }   /* 2秒超时 */
        break;

    /* ---- 状态：待服务器回复的IPD ---- */
    case ES_WAIT_IPD:
        if (s_ipd_ready)    { s_state = ES_READY; s_tick = 0; }
        else if (s_tick > 1000) { s_state = ES_READY; }   /* 5秒超时：千问API可能1-3秒 */
        break;

    /* ---- 状态11: 出错1秒后自动重试整个握手 ---- */
    case ES_ERROR:
        s_at_ok = 0; s_at_err = 0; s_busy = 0; s_esp_sent = 0; s_sent_tick = 0;
        if (s_tick > 1000)   /* 5秒 */
        {
            /* 如果CWJAP失败次数已达上限，直接进入离线状态 */
            if (s_cwjap_try >= MAX_CWJAP_RETRY)
            {
                printf("[ESP] too many CWJAP failures, going offline\r\n");
                s_state = ES_OFFLINE;
            }
            else
            {
                s_init_try = 0;
                s_cipstart_try = 0;
                s_state    = ES_INIT;
            }
            s_tick = 0;
            printf("[ESP] retry handshake\r\n");
        }
        break;

    /* ---- 状态12: WiFi不可用，保持离线，ESP8266_IsReady()返回0 ---- */
    case ES_OFFLINE:
        /* 什么都不做，保持此状态直到用户手动重置 */
        break;
    }
}

/* ============================================================
 *  公共API
 * ============================================================ */

/* 是否就绪（可发送数据） */
uint8_t ESP8266_IsReady(void)
{
    if (s_fetch_active) return 0;
    return (s_state == ES_READY ||

            s_state == ES_SEND_PROMPT ||
            s_state == ES_SEND_DATA || s_state == ES_WAIT_IPD) ? 1 : 0;
}

/* 获取当前状态码（供UI显示） */
uint8_t ESP8266_GetStatus(void)
{
    return (uint8_t)s_state;
}

/* 有消息排队待发送时非零(HELLO, STATUS, BOX...)。调用此函数检查ESP8266_SendText()是否会拒绝新请求。 */
uint8_t ESP8266_SendIsPending(void)
{
    return (uint8_t)s_send_pending;
}

/* 排队发送一行文本（非阻塞，返回0=失败/不可用） */
uint8_t ESP8266_SendText(const char *text)
{
    uint16_t len = (uint16_t)strlen(text);
    /* 预留 1 字节给行尾 \n：所有调用方都不带 \n，
       server.py 按 \n 分行解析，缺失会导致 last_seen 永远 null。 */
    if (len == 0 || len >= ESP_SEND_LEN - 1) return 0;
    if (s_send_pending) return 0;
    if (!ESP8266_IsReady()) return 0;
    memcpy(s_sendbuf, text, len);
    /* 若末尾不是 \n，则补一个 \n。已经带 \n 的不重复追加。 */
    if (len == 0 || s_sendbuf[len - 1] != '\n')
    {
        s_sendbuf[len++] = '\n';
    }
    s_sendlen = len;
    s_send_pending = 1;
    return 1;
}

/* 取下一行服务器回复（无数据返回0） */
uint8_t ESP8266_GetResponse(char *out, uint8_t maxlen)
{
    uint8_t i;
    if (!s_ipd_ready || maxlen == 0) return 0;
    for (i = 0; i < s_ipd_len && i < (maxlen - 1); i++)
        out[i] = s_ipd[i];
    out[i] = 0;
    s_ipd_ready = 0;
    s_ipd_len   = 0;
    return 1;
}

/* ============================================================
 *  非阻塞 HTTP GET 对外接口
 * ============================================================ */
uint8_t ESP8266_FetchBusy(void)
{
    return s_fetch_active;
}

uint8_t ESP8266_FetchStart(const char *host, const char *path)
{
    if (s_fetch_active) return 1;
    if (!host || !path) return 2;
    if (strlen(host) >= ESP_FETCH_HOST_LEN) return 2;
    if (strlen(path) >= ESP_FETCH_PATH_LEN) return 2;
    /* 一行上行文本正排队或正处在CIPSEND握手中间时拒绝借链路：
       此刻模块可能还在等AT+CIPSEND声明的载荷字节，FS_UNLINK要发的
       AT+CIPCLOSE会被当作TCP载荷吐出去(服务器日志里出现过
       未识别指令 'AT+CIPCLOSE')，模块也会就此进入混乱态，
       导致后续AT+CIPSTART连续返回ERROR。等本次发送结束再抓天气。 */
    if (ESP8266_SendIsPending()) return 1;
    if (!ESP8266_IsReady()) return 2;

    strcpy(s_fetch_host, host);
    strcpy(s_fetch_path, path);
    s_fetch_result     = 0;
    s_fetch_len        = 0;
    s_fetch_rx[0]      = 0;
    s_fetch_state      = FS_UNLINK;
    s_fetch_unlinked   = 0;
    s_esp_sent         = 0;
    s_fetch_start_tick = s_tick;
    s_fetch_active     = 1;

    return 0;
}

uint8_t ESP8266_FetchPoll(char *out, uint16_t maxlen)
{
    uint16_t i;
    uint8_t  r;

    if (s_fetch_active) return 0;
    r = s_fetch_result;
    if (r == 0) return 0;

    if (out && maxlen)
    {
        if (r == 1)
        {
            for (i = 0; i < s_fetch_len && i < (maxlen - 1); i++)
                out[i] = s_fetch_rx[i];
            out[i] = 0;
        }
        else
        {
            out[0] = 0;
        }
    }
    s_fetch_result = 0;
    return r;
}

