/******************************************************
**  codebox.h - 6-digit pickup code table (Flash-backed)
**
**  Stores up to CODEBOX_COUNT entries in STM32F407VE
**  Flash Sector 11 (0x080E0000, 128KB, last sector).
**  Each entry maps a locker box to a phone tail + pickup code.
**
**  Layout at 0x080E0000:
**    offset 0 : uint32_t magic   (CODEBOX_MAGIC)
**    offset 4 : uint32_t reserved
**    offset 8 : CodeEntry[CODEBOX_COUNT]
**
**  Whole table is erased + rewritten on every modification
**  (deposit / clear). Read-back into RAM cache at CodeBox_Init.
******************************************************/
#ifndef __CODEBOX_H
#define __CODEBOX_H

#include "stm32f4xx.h"

#define CODEBOX_COUNT        4      /* number of lockers                  */
#define CODE_LEN             6      /* 6-digit pickup code                 */
#define PHONE_LEN            6      /* 6-digit phone tail                  */

/* 持久化记录(14字节)。以ASCII数字'0'..'9'存储。 */
typedef struct {
    uint8_t box_id;                  /* 1..4 (0 = slot unused)              */
    uint8_t in_use;                  /* 1 = occupied, 0 = free              */
    uint8_t phone[PHONE_LEN];        /* depositor phone tail (6 ASCII)      */
    uint8_t code[CODE_LEN];          /* pickup code (6 ASCII)               */
} CodeEntry;

/* 扇区起始处写入魔数,用于检测已擦除/空白扇区。 */
#define CODEBOX_MAGIC        0xCFB88125UL

/* 从Flash加载取件码表到RAM缓存。如果魔数无效(空白芯片或首次启动),缓存初始化为全空并写入新表到Flash。 */
void    CodeBox_Init(void);

/* 投件: 查找第一个空闲柜格,分配它,根据手机尾号+柜格ID生成6位取件码,持久化到Flash。 phone: 6位ASCII数字 out_code: 填充6位ASCII数字(第6位为结束符) 成功返回柜格ID 1..4, 全部占用返回0。 */
uint8_t CodeBox_Deposit(const uint8_t phone[PHONE_LEN], uint8_t out_code[CODE_LEN + 1]);

/* 直接设置: 向指定柜格写入指定取件码。用于云端SET指令,使通过云端H5投件的包裹(取件码由云端生成)仍可在柜机键盘输入该取件码本地取件。手机尾号留空(云端取件基于取件码)。成功返回1, box_id无效返回0。 */
uint8_t CodeBox_DirectSet(uint8_t box_id, const uint8_t code[CODE_LEN]);

/* 通过6位取件码取件。匹配返回柜格ID 1..4, 未匹配返回0。 */
uint8_t CodeBox_PickupByCode(const uint8_t code[CODE_LEN]);

/* 通过6位手机尾号取件。匹配返回柜格ID 1..4, 未匹配返回0。 */
uint8_t CodeBox_PickupByPhone(const uint8_t phone[PHONE_LEN]);

/* 标记柜格为空闲(包裹已取走)。持久化到Flash。任何box_id调用都安全; 当前未占用则无操作。 */
void    CodeBox_Clear(uint8_t box_id);

/* 记录的只读快照。成功返回1, box_id无效或未使用返回0。 */
uint8_t CodeBox_GetEntry(uint8_t box_id, CodeEntry *out);

/* 当前已占用柜格数(0..CODEBOX_COUNT)。 */
uint8_t CodeBox_CountUsed(void);

#endif /* __CODEBOX_H */
