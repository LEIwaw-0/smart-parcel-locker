/******************************************************
**  codebox.c - 6-digit pickup code table (Flash-backed)
**
**  See codebox.h for layout. Uses STM32F4 standard peripheral
**  FLASH_Unlock / FLASH_EraseSector / FLASH_ProgramByte / FLASH_Lock.
**  Sector 11 @ 0x080E0000 (128KB, last sector in 512KB F407VE).
**
**  Code generation: simple xorshift LFSR seeded from the 6 phone digits
**  and box_id. Produces 6 ASCII digits. Not cryptographically secure;
**  adequate for a 4-box parcel locker.
******************************************************/
#include "codebox.h"
#include "stm32f4xx_flash.h"
#include <stdio.h>
#include <string.h>

extern uint32_t loop_tick;

#define CODEBOX_FLASH_ADDR   0x080E0000UL
#define CODEBOX_SECTOR       FLASH_Sector_11
#define CODEBOX_VOLTAGE      VoltageRange_3

static CodeEntry g_table[CODEBOX_COUNT];

/* ---------- 内部辅助函数 ---------- */

/* xorshift32状态,由手机尾号+box_id播种 */
static uint32_t g_lfsr;

static uint32_t lfsr_next(void)
{
    uint32_t x = g_lfsr;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    g_lfsr = x;
    return x;
}

/* 根据手机尾号+柜格ID生成6位ASCII取件码。phone: 6位ASCII数字。out: 6位ASCII数字+结束符。 */
static void gen_code(const uint8_t phone[PHONE_LEN], uint8_t box_id,
                     uint8_t out[CODE_LEN + 1])
{
    uint8_t i;
    uint32_t seed = 0x12345678UL;
    for (i = 0; i < PHONE_LEN; i++)
        seed = seed * 131 + (uint32_t)phone[i];
    seed = seed * 257 + (uint32_t)box_id + loop_tick;
    if (seed == 0) seed = 0x55AA55AA;
    g_lfsr = seed;
    for (i = 0; i < CODE_LEN; i++)
        out[i] = (uint8_t)('0' + (lfsr_next() % 10));
    out[CODE_LEN] = 0;
}

/* 将RAM缓存(魔数+表)写入Flash第11扇区。先擦除整个扇区,再逐字节编程。 */
static void flash_flush(void)
{
    uint8_t  buf[sizeof(uint32_t) * 2 + sizeof(g_table)];
    uint32_t addr = CODEBOX_FLASH_ADDR;
    uint16_t i;

    /* 构建镜像: 魔数 + 保留 + 表 */
    {
        uint32_t magic = CODEBOX_MAGIC;
        memcpy(buf + 0, &magic, 4);
        memset(buf + 4, 0, 4);
        memcpy(buf + 8, g_table, sizeof(g_table));
    }

    FLASH_Unlock();
    FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR |
                    FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);

    if (FLASH_EraseSector(CODEBOX_SECTOR, CODEBOX_VOLTAGE) != FLASH_COMPLETE)
    {
        FLASH_Lock();
        return;   /* 擦除失败: 调用方保留RAM副本,下次写入时重试 */
    }

    for (i = 0; i < sizeof(buf); i++)
    {
        while (FLASH_ProgramByte(addr + i, buf[i]) != FLASH_COMPLETE)
            ;
    }
    FLASH_Lock();
}

/* 从Flash加载取件码表。魔数不匹配(空白或损坏)时,重置表为空并写入新副本。 */
static void flash_load(void)
{
    const uint8_t *p = (const uint8_t *)CODEBOX_FLASH_ADDR;
    uint32_t magic;
    memcpy(&magic, p, 4);
    if (magic != CODEBOX_MAGIC)
    {
        memset(g_table, 0, sizeof(g_table));
        flash_flush();
        return;
    }
    memcpy(g_table, p + 8, sizeof(g_table));
}

/* ---------- 公共API ---------- */

void CodeBox_Init(void)
{
    uint8_t i;
    flash_load();
    printf("[CODEBOX] Init: %d boxes used\r\n", CodeBox_CountUsed());
    for (i = 0; i < CODEBOX_COUNT; i++)
    {
        if (g_table[i].in_use)
            printf("[CODEBOX]   box%d code=%.6s phone=%.6s\r\n",
                   i + 1, g_table[i].code, g_table[i].phone);
    }
}

uint8_t CodeBox_Deposit(const uint8_t phone[PHONE_LEN], uint8_t out_code[CODE_LEN + 1])
{
    uint8_t i;
    uint8_t box = 0;

    for (i = 0; i < CODEBOX_COUNT; i++)
    {
        if (!g_table[i].in_use)
        {
            box = (uint8_t)(i + 1);
            break;
        }
    }
    if (box == 0) return 0;   /* 全部已满 */

    {
        CodeEntry *e = &g_table[box - 1];
        e->box_id = box;
        e->in_use = 1;
        memcpy(e->phone, phone, PHONE_LEN);
        gen_code(phone, box, e->code);
        if (out_code)
            memcpy(out_code, e->code, CODE_LEN + 1);

        printf("[CODEBOX] Deposit: phone=%.6s -> box=%d code=%.6s\r\n",
               phone, box, e->code);
    }
    flash_flush();
    return box;
}

/* 云端SET: 将云端分配的取件码写入指定柜格,使用户可在柜机键盘输入该取件码本地取件。手机尾号留空因为云端取件基于取件码。 */
uint8_t CodeBox_DirectSet(uint8_t box_id, const uint8_t code[CODE_LEN])
{
    uint8_t idx;
    if (box_id < 1 || box_id > CODEBOX_COUNT) return 0;
    idx = (uint8_t)(box_id - 1);
    {
        CodeEntry *e = &g_table[idx];
        e->box_id = box_id;
        e->in_use = 1;
        memset(e->phone, 0, PHONE_LEN);
        memcpy(e->code, code, CODE_LEN);
    }
    printf("[CODEBOX] DirectSet: box=%d code=%.6s\r\n", box_id, code);
    flash_flush();
    return 1;
}

uint8_t CodeBox_PickupByCode(const uint8_t code[CODE_LEN])
{
    uint8_t i;
    printf("[CODEBOX] PickupByCode query=%.6s\r\n", code);
    for (i = 0; i < CODEBOX_COUNT; i++)
    {
        printf("[CODEBOX]   slot%d in_use=%d code=%.6s match=%d\r\n",
               i + 1, g_table[i].in_use, g_table[i].code,
               g_table[i].in_use && memcmp(g_table[i].code, code, CODE_LEN) == 0);
        if (g_table[i].in_use &&
            memcmp(g_table[i].code, code, CODE_LEN) == 0)
        {
            printf("[CODEBOX] HIT box=%d\r\n", g_table[i].box_id);
            return g_table[i].box_id;
        }
    }
    printf("[CODEBOX] no match\r\n");
    return 0;
}

uint8_t CodeBox_PickupByPhone(const uint8_t phone[PHONE_LEN])
{
    uint8_t i;
    printf("[CODEBOX] PickupByPhone query=%.6s\r\n", phone);
    for (i = 0; i < CODEBOX_COUNT; i++)
    {
        if (g_table[i].in_use &&
            memcmp(g_table[i].phone, phone, PHONE_LEN) == 0)
            return g_table[i].box_id;
    }
    return 0;
}

void CodeBox_Clear(uint8_t box_id)
{
    uint8_t idx;
    if (box_id < 1 || box_id > CODEBOX_COUNT) return;
    idx = (uint8_t)(box_id - 1);
    if (!g_table[idx].in_use) return;
    printf("[CODEBOX] Clear box=%d\r\n", box_id);
    g_table[idx].in_use = 0;
    g_table[idx].box_id = 0;
    memset(g_table[idx].phone, 0, PHONE_LEN);
    memset(g_table[idx].code, 0, CODE_LEN);
    flash_flush();
}

uint8_t CodeBox_GetEntry(uint8_t box_id, CodeEntry *out)
{
    uint8_t idx;
    if (box_id < 1 || box_id > CODEBOX_COUNT) return 0;
    idx = (uint8_t)(box_id - 1);
    if (!g_table[idx].in_use) return 0;
    if (out) *out = g_table[idx];
    return 1;
}

uint8_t CodeBox_CountUsed(void)
{
    uint8_t i, n = 0;
    for (i = 0; i < CODEBOX_COUNT; i++)
        if (g_table[i].in_use) n++;
    return n;
}
