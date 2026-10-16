/**
  ******************************************************************************
  * @file    lock.c
  * @brief   快递柜4路电磁锁控制
  *          后端1(默认): STM32 GPIO直驱继电器IN (PB8/PB9/PB12/PB13)
  *                       复刻 relay 工程已验证的推挽+低电平触发接法
  *          后端2(可选): PCF8574 P0~P3输出 (LOCK_BACKEND_GPIO=0, 需足量5V)
  *          锁反馈: 两种后端均经 PCF8574 P4~P7 读取
  ******************************************************************************
  */
#include "lock.h"
#include "pcf8574.h"
#include "delay.h"
#include <stdio.h>

/* 向硬件写输出失败时的重试次数(仅PCF后端需要) */
#define LOCK_I2C_RETRY        3u

/* 当前P0~P3输出影子: bit0~bit3对应锁1~4, 1=该路请求通电 */
static uint8_t  lock_out_bits   = 0;
/* 各锁开始通电的CYCCNT周期数, 0=未通电 */
static uint32_t lock_open_cyc[LOCK_COUNT] = {0, 0, 0, 0};

#if LOCK_BACKEND_GPIO
/* 4路锁对应引脚表: 锁1->PB8 锁2->PB9 锁3->PB12 锁4->PB13 */
static const uint16_t lock_pin[LOCK_COUNT] = {LOCK1_PIN, LOCK2_PIN, LOCK3_PIN, LOCK4_PIN};
#endif

static void Lock_DWT_Init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

/* PCF后端: 影子字节按触发电平宏转换 */
#if !LOCK_BACKEND_GPIO
static uint8_t Lock_OutputByte(void)
{
#if LOCK_ACTIVE_LOW
    /* 低电平触发: 请求通电的位输出0, 其余输出1(弱高, 光耦不导通) */
    return (uint8_t)(0xF0 | ((~lock_out_bits) & 0x0F));
#else
    /* 高电平触发: 请求通电的位输出1, 其余输出0; 高4位恒1保持P4~P7输入 */
    return (uint8_t)(0xF0 | lock_out_bits);
#endif
}
#endif

/* 把输出影子写到硬件, 成功返回0, 失败返回1 */
static uint8_t Lock_Flush(void)
{
#if LOCK_BACKEND_GPIO
    uint8_t i;
    /* 低电平触发(跳线帽L): 通电=输出低(ResetBits), 断电=输出高(SetBits) */
    for (i = 0; i < LOCK_COUNT; i++)
    {
        uint8_t on = (lock_out_bits & (1u << i)) ? 1 : 0;
        if ((on && LOCK_ACTIVE_LOW) || (!on && !LOCK_ACTIVE_LOW))
            GPIO_ResetBits(LOCK_GPIO_PORT, lock_pin[i]);
        else
            GPIO_SetBits(LOCK_GPIO_PORT, lock_pin[i]);
    }
    return 0;   /* 本地GPIO写不会失败 */
#else
    uint8_t data = Lock_OutputByte();
    uint8_t retry;
    for (retry = 0; retry < LOCK_I2C_RETRY; retry++)
    {
        if (PCF8574_WriteByte(data) == 0) return 0;
        delay_ms(3);
    }
    return 1;
#endif
}

void Lock_Init(void)
{
#if !LOCK_BACKEND_GPIO
    uint8_t err;
#endif
    Lock_DWT_Init();
    lock_out_bits = 0;
    lock_open_cyc[0] = lock_open_cyc[1] = 0;
    lock_open_cyc[2] = lock_open_cyc[3] = 0;

#if LOCK_BACKEND_GPIO
    {
        GPIO_InitTypeDef GPIO_InitStructure;
        uint8_t i;
        RCC_AHB1PeriphClockCmd(LOCK_GPIO_CLK, ENABLE);

        /* 先把4个脚置为安全电平(低电平触发=高=释放), 再配成输出,
         * 避免配置切换瞬间继电器误吸合 (与relay工程一致) */
        GPIO_SetBits(LOCK_GPIO_PORT, LOCK_ALL_PINS);

        GPIO_InitStructure.GPIO_Pin   = LOCK_ALL_PINS;
        GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_OUT;
        GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;   /* 推挽, 复刻relay工程 */
        GPIO_InitStructure.GPIO_PuPd  = GPIO_PuPd_UP;
        GPIO_InitStructure.GPIO_Speed = GPIO_Low_Speed;
        GPIO_Init(LOCK_GPIO_PORT, &GPIO_InitStructure);

        for (i = 0; i < LOCK_COUNT; i++)
            GPIO_SetBits(LOCK_GPIO_PORT, lock_pin[i]);   /* 全部释放 */
    }
    printf("\xA1\xBE\xCB\xF8\xBF\xD8\xA1\xBF\xB3\xF5\xCA\xBC\xBB\xAF\xCD\xEA\xB3\xC9(GPIO\xD6\xB1\xC7\xFD PB8/PB9/PB12/PB13), \xC8\xAB\xB2\xBF\xBC\xCC\xB5\xE7\xC6\xF7\xB6\xCF\xB5\xE7\r\n");
#else
    err = Lock_Flush();
    printf("\xA1\xBE\xCB\xF8\xBF\xD8\xA1\xBF\xB3\xF5\xCA\xBC\xBB\xAF\xCD\xEA\xB3\xC9, \xC8\xAB\xB2\xBF\xBC\xCC\xB5\xE7\xC6\xF7\xB6\xCF\xB5\xE7, \xD0\xB4PCF8574=0x%02X, %s, \xB4\xA5\xB7\xA2\xB7\xBD\xCA\xBD=%s\r\n",
           Lock_OutputByte(),
           err ? "\xCA\xA7\xB0\xDC!!!" : "\xB3\xC9\xB9\xA6",
           LOCK_ACTIVE_LOW ? "\xB5\xCD\xB5\xE7\xC6\xBD\xD3\xD0\xD0\xA7(\xBC\xCC\xB5\xE7\xC6\xF7\xB0\xE5\xCC\xF8\xCF\xDF\xC3\xB1\xD4\xDAL)" : "\xB8\xDF\xB5\xE7\xC6\xBD\xD3\xD0\xD0\xA7");
#endif

#if !LOCK_BACKEND_GPIO
    err = Lock_Flush();
    (void)err;
#else
    Lock_Flush();
#endif
}

void Lock_Open(uint8_t id)
{
    uint8_t i;
#if !LOCK_BACKEND_GPIO
    uint8_t err;
#endif
    if (id < 1 || id > LOCK_COUNT) return;

    /* 互斥: 只保留目标锁通电 */
    lock_out_bits = (uint8_t)(1u << (id - 1));
    for (i = 0; i < LOCK_COUNT; i++)
    {
        if (i == (id - 1))
        {
            /* 安全: 同一把锁通电期间重复触发不重置计时, 保证单次通电绝不超过
             * LOCK_ON_TIME_MS(<3秒硬约束), 防止快速连点导致线圈持续通电过热 */
            if (lock_open_cyc[i] == 0) lock_open_cyc[i] = DWT->CYCCNT;
        }
        else
        {
            lock_open_cyc[i] = 0;
        }
    }
    Lock_Flush();
#if !LOCK_BACKEND_GPIO
    {
        uint8_t rb_v;
        err = 0;
        uint8_t ex;
        for (ex = 0; ex < 2; ex++)
        {
            delay_ms(20);
            if (PCF8574_ReadByte(&rb_v) == 0 &&
                (uint8_t)(rb_v & 0x0F) == (uint8_t)(Lock_OutputByte() & 0x0F))
            {
                err = 0;
                break;
            }
            if (Lock_Flush() == 0) err = 0;
        }
    }
#endif
#if LOCK_BACKEND_GPIO
    printf("\xA1\xBE\xBF\xAA\xCB\xF8\xA1\xBF" "BOX%d \xBC\xCC\xB5\xE7\xC6\xF7\xCD\xA8\xB5\xE7(\xBB\xA5\xB3\xE2), \xCA\xE4\xB3\xF6\xD1\xDA\xC2\xEB=0x%X, GPIO\xC7\xFD\xB6\xAF\xB3\xC9\xB9\xA6\r\n",
           id, lock_out_bits);
#else
    printf("\xA1\xBE\xBF\xAA\xCB\xF8\xA1\xBF" "BOX%d \xBC\xCC\xB5\xE7\xC6\xF7\xCD\xA8\xB5\xE7(\xBB\xA5\xB3\xE2), \xD0\xB4\xC8\xEB\xD7\xD6\xBD\xDA=0x%02X, I2C\xB7\xA2\xCB\xCD%s\r\n",
           id, Lock_OutputByte(), err ? "\xCA\xA7\xB0\xDC(\xD2\xD1\xD6\xD8\xCA\xD4" "3\xB4\xCE)!!!" : "\xB3\xC9\xB9\xA6");
#endif
}

void Lock_Close(uint8_t id)
{
#if !LOCK_BACKEND_GPIO
    uint8_t err;
#endif
    if (id < 1 || id > LOCK_COUNT) return;
    lock_out_bits &= (uint8_t)~(1u << (id - 1));
    lock_open_cyc[id - 1] = 0;
#if !LOCK_BACKEND_GPIO
    err = Lock_Flush();
#else
    Lock_Flush();
#endif
#if LOCK_BACKEND_GPIO
    printf("\xA1\xBE\xB9\xD8\xCB\xF8\xA1\xBF" "BOX%d \xBC\xCC\xB5\xE7\xC6\xF7\xB6\xCF\xB5\xE7, \xCA\xE4\xB3\xF6\xD1\xDA\xC2\xEB=0x%X, GPIO\xC7\xFD\xB6\xAF\xB3\xC9\xB9\xA6\r\n",
           id, lock_out_bits);
#else
    printf("\xA1\xBE\xB9\xD8\xCB\xF8\xA1\xBF" "BOX%d \xBC\xCC\xB5\xE7\xC6\xF7\xB6\xCF\xB5\xE7, \xD0\xB4\xC8\xEB\xD7\xD6\xBD\xDA=0x%02X, I2C\xB7\xA2\xCB\xCD%s\r\n",
           id, Lock_OutputByte(), err ? "\xCA\xA7\xB0\xDC(\xD2\xD1\xD6\xD8\xCA\xD4" "3\xB4\xCE)!!!" : "\xB3\xC9\xB9\xA6");
#endif
}

uint8_t Lock_Tick(void)
{
    uint8_t closed_mask = 0;
    uint8_t i;

    for (i = 0; i < LOCK_COUNT; i++)
    {
        if (lock_open_cyc[i] != 0)
        {
            /* uint32周期数差值在CYCCNT回绕时仍正确(模2^32), 2.5秒=4.2亿周期 */
            uint32_t elapsed_cyc = DWT->CYCCNT - lock_open_cyc[i];
            uint32_t limit_cyc   = (uint32_t)LOCK_ON_TIME_MS * (SystemCoreClock / 1000u);
            if (elapsed_cyc >= limit_cyc)
            {
                lock_out_bits &= (uint8_t)~(1u << i);
                lock_open_cyc[i] = 0;
                closed_mask |= (uint8_t)(1u << i);
            }
        }
    }

    if (closed_mask)
    {
        uint8_t err = Lock_Flush();
        for (i = 0; i < LOCK_COUNT; i++)
        {
            if (closed_mask & (1u << i))
            {
#if LOCK_BACKEND_GPIO
                printf("\xA1\xBE\xB3\xAC\xCA\xB1\xB1\xA3\xBB\xA4\xA1\xBF" "BOX%d \xCD\xA8\xB5\xE7\xC2\xFA%dms\xD7\xD4\xB6\xAF\xB6\xCF\xB5\xE7, GPIO\xC7\xFD\xB6\xAF\xB3\xC9\xB9\xA6\r\n",
                       i + 1, LOCK_ON_TIME_MS);
#else
                printf("\xA1\xBE\xB3\xAC\xCA\xB1\xB1\xA3\xBB\xA4\xA1\xBF" "BOX%d \xCD\xA8\xB5\xE7\xC2\xFA%dms\xD7\xD4\xB6\xAF\xB6\xCF\xB5\xE7, I2C\xB7\xA2\xCB\xCD%s\r\n",
                       i + 1, LOCK_ON_TIME_MS, err ? "\xCA\xA7\xB0\xDC(\xD2\xD1\xD6\xD8\xCA\xD4" "3\xB4\xCE)!!!" : "\xB3\xC9\xB9\xA6");
#endif
            }
        }
    }
    return closed_mask;
}

uint8_t Lock_IsOpen(uint8_t id)
{
    if (id < 1 || id > LOCK_COUNT) return 0;
    return (lock_out_bits & (1u << (id - 1))) ? 1 : 0;
}

uint8_t Lock_GetActiveMask(void)
{
    return lock_out_bits;
}

/* 读回硬件实际通电掩码(bit0~3: 1=该路处于通电电平); 0xFF=读取失败 */
uint8_t Lock_HW_ReadBack(void)
{
#if LOCK_BACKEND_GPIO
    uint8_t m = 0;
    uint8_t i;
    for (i = 0; i < LOCK_COUNT; i++)
    {
        uint8_t pin_low = (GPIO_ReadInputDataBit(LOCK_GPIO_PORT, lock_pin[i]) == Bit_RESET);
#if LOCK_ACTIVE_LOW
        if (pin_low) m |= (uint8_t)(1u << i);   /* 低电平=通电 */
#else
        if (!pin_low) m |= (uint8_t)(1u << i);  /* 高电平=通电 */
#endif
    }
    return m;
#else
    uint8_t raw;
    if (PCF8574_ReadByte(&raw) != 0) return 0xFF;
#if LOCK_ACTIVE_LOW
    return (uint8_t)((~raw) & 0x0F);
#else
    return (uint8_t)(raw & 0x0F);
#endif
#endif
}

uint8_t Lock_ReadFeedback(uint8_t id)
{
    uint8_t raw;
    if (id < 1 || id > LOCK_COUNT) return 2;
    if (PCF8574_ReadByte(&raw) != 0) return 2;
    /* P4~P7对应锁1~4, 高电平=锁舌收回=已开锁 */
    return (raw & (1u << (id + 3))) ? 1 : 0;
}
