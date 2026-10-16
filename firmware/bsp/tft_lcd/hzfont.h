#ifndef __HZFONT_H
#define __HZFONT_H
#include "stm32f4xx.h"

typedef struct {
    uint16_t gbk_code;
    const uint8_t bits[32];
} HZFont_t;

extern const HZFont_t HZFont[];
extern const int HZFont_Count;
const uint8_t *HZ_Lookup(uint16_t gbk_code);

#endif
