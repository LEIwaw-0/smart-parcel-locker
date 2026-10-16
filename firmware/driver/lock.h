/**
  ******************************************************************************
  * @file    lock.h
  * @brief   快递柜4路电磁锁控制 (PCF8574 P0~P3输出 + P4~P7反馈)
  *
  * 硬件映射闭环:
  *   PCF8574(I2C1 PB6/PB7)
  *     P0~P3 -> 继电器IN1~IN4 -> 电磁锁正极(12V, NO触点)
  *     P4~P7 <- 电磁锁反馈线(蓝/白无源触点) (高=锁舌收回/已开锁, 低=锁舌弹出/已上锁)
  *
  * 安全设计:
  *   1. 单次开锁通电 LOCK_ON_TIME_MS=2500ms 后由 Lock_Tick() 非阻塞自动断电
  *      (硬约束: 电磁锁线圈通电严禁>=3秒, 见编译期 #error)
  *   2. 4把锁互斥: 任一时刻最多1把锁通电
  *   3. 断电写PCF8574失败时重试3次
  ******************************************************************************
  */
#ifndef __LOCK_H
#define __LOCK_H

#include "stm32f4xx.h"

#define LOCK_COUNT            4       /* 锁/格口数量 */

/* 单次开锁通电时长(ms). 硬约束: 必须 < 3000 */
#define LOCK_ON_TIME_MS       2000u
#if LOCK_ON_TIME_MS >= 3000u
#error "LOCK_ON_TIME_MS must be < 3000ms to protect the solenoid coil!"
#endif

/* 继电器触发电平选择:
 *   0 = 高电平触发开锁(PCF8574对应位输出1)
 *       注意: PCF8574输出高电平时仅为内部弱上拉(约0.1mA), 驱动能力很弱,
 *       若日志显示I2C写入成功但继电器无吸合声, 多为高电平带不动光耦输入
 *   1 = 低电平触发开锁(PCF8574对应位输出0, 灌电流可达25mA, 驱动可靠)
 *       多数蓝色/红色4路继电器模块为低电平触发, 遇上述现象改本宏为1重烧
 * 改本宏后继电器静态电平相反, 切换前务必确认模块触发电平, 防止上电即吸合 */
#define LOCK_ACTIVE_LOW       1

/* 输出后端选择:
 *   1 = STM32 GPIO直驱继电器IN (PB8/PB9/PB12/PB13, 复刻relay工程已验证接法)
 *       PCF8574在5V轨仅4.5V时灌电流不足, 端口被光耦抬回高电平(写0xFE读回0xFF),
 *       STM32引脚低边导通电阻仅数十欧, 吸合可靠; PCF8574保留为P4~P7反馈输入
 *   0 = PCF8574 P0~P3输出 (需5V轨足量, 保留可回退) */
#define LOCK_BACKEND_GPIO     1

/* GPIO直驱引脚映射(均为FT 5V容忍脚): 锁1->PB8 锁2->PB9 锁3->PB12 锁4->PB13 */
#define LOCK_GPIO_CLK         RCC_AHB1Periph_GPIOB
#define LOCK_GPIO_PORT        GPIOB
#define LOCK1_PIN             GPIO_Pin_8
#define LOCK2_PIN             GPIO_Pin_9
#define LOCK3_PIN             GPIO_Pin_12
#define LOCK4_PIN             GPIO_Pin_13
#define LOCK_ALL_PINS         (LOCK1_PIN | LOCK2_PIN | LOCK3_PIN | LOCK4_PIN)

/* 初始化锁控: DWT计时 + 全部锁立即断电. 须在 PCF8574_Init() 之后调用 */
void Lock_Init(void);

/* 打开指定锁(1~4): 继电器吸合通电, 同时关闭其它锁(互斥) */
void Lock_Open(uint8_t id);

/* 关闭指定锁(1~4): 立即断电 */
void Lock_Close(uint8_t id);

/* 锁超时维护, 主循环周期调用(建议<=10ms).
 * 返回值: 本次刚自动断电的锁bitmask(bit0=锁1...bit3=锁4), 0=无变化 */
uint8_t Lock_Tick(void);

/* 查询锁当前是否通电(软件输出状态), 1~4, 非法编号返回0 */
uint8_t Lock_IsOpen(uint8_t id);

/* 读取当前期望的通电掩码(bit0=锁1...bit3=锁4), 供诊断 */
uint8_t Lock_GetActiveMask(void);

/* 读回硬件实际状态掩码(1=该路处于通电电平); 0xFF=读取失败(I2C后端) */
uint8_t Lock_HW_ReadBack(void);

/* 读取锁硬件反馈: 1=已开锁, 0=已上锁, 2=I2C读取失败, 非法编号返回2 */
uint8_t Lock_ReadFeedback(uint8_t id);

#endif /* __LOCK_H */
