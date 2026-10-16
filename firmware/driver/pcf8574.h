/**
  ******************************************************************************
  * @file    pcf8574.h
  * @brief   PCF8574 8位I2C准双向IO扩展驱动 (快递柜锁控板)
  *
  * 硬件映射闭环:
  *   SCL -> STM32 PB6 (I2C1_SCL, 复用开漏, 外部4.7k上拉)
  *   SDA -> STM32 PB7 (I2C1_SDA, 复用开漏, 外部4.7k上拉)
  *   P0~P3 -> 继电器 IN1~IN4
  *   P4~P7 -> 电磁锁反馈白线 (高=已开锁 低=已上锁)
  *
  * 地址自适应:
  *   PCF8574  基地址 0x20 (A0/A1/A2全接地)
  *   PCF8574A 基地址 0x38 (两种芯片印字不同, 模块买错时仍可通信)
  *   PCF8574_Init() 会自动扫描两个地址, 用应答的那个
  *
  * PCF8574 无寄存器指针, 单字节直读直写
  ******************************************************************************
  */
#ifndef __PCF8574_H
#define __PCF8574_H

#include "stm32f4xx.h"

/* 两种可能的7位I2C地址 */
#define PCF8574_ADDR           0x20
#define PCF8574A_ADDR          0x38

/* 锁控板初始/安全输出: 低4位0(继电器全断=上锁), 高4位1(输入高阻)
 * 注意: 该电平对应"高电平触发"继电器; 低电平触发模块见 lock.h LOCK_ACTIVE_LOW */
#define PCF8574_SAFE_OUTPUT    0xFF  /* 8脚全输入/高阻: GPIO后端P0-P3悬空, P4-P7反馈输入; 低电平触发下也是全释放安全态 */

/* 初始化 I2C1(PB6/PB7,100kHz), 扫描0x20/0x38确定芯片地址, 写安全电平
 * 返回: 实际应答的7位地址(0x20或0x38); 0=总线上未发现设备 */
uint8_t PCF8574_Init(void);

/* 写1字节. 返回 0=成功, 1=总线超时/无应答 */
uint8_t PCF8574_WriteByte(uint8_t data);

/* 读1字节. 返回 0=成功(*val有效), 1=总线超时/无应答 */
uint8_t PCF8574_ReadByte(uint8_t *val);

/* 读取当前使用的I2C地址(未扫到设备时返回默认0x20) */
uint8_t PCF8574_GetAddr(void);

/* I2C总线物理层自检结果(不依赖从机, 只测MCU引脚+上拉+短路) */
typedef struct {
    uint8_t scl_idle;        /* 空闲SCL电平, 1=高 0=被钳低 */
    uint8_t sda_idle;        /* 空闲SDA电平, 1=高 0=被钳低 */
    uint8_t scl_pulldown_ok; /* 主动拉低SCL回读为0, 1=正常 */
    uint8_t sda_pulldown_ok; /* 主动拉低SDA回读为0, 1=正常 */
    uint8_t scl_release_ok;  /* 释放SCL回读为1, 1=上拉正常 */
    uint8_t sda_release_ok;  /* 释放SDA回读为1, 1=上拉正常 */
} PCF_BusDiag_t;

/* 执行总线物理层自检, 结果填入*d. 须在PCF8574_Init之后调用 */
void PCF8574_BusDiag(PCF_BusDiag_t *d);
/* 全地址扫描: 探测0x01~0x7E所有7位地址, 应答的地址依次写入addr_list
 * 返回应答设备数量(最多max_count个). 耗时约60ms, 仅诊断/初始化用 */
uint8_t PCF8574_ScanBus(uint8_t *addr_list, uint8_t max_count);

#endif /* __PCF8574_H */
