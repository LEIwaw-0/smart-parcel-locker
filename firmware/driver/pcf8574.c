/**
  ******************************************************************************
  * @file    pcf8574.c
  * @brief   PCF8574 8位I2C准双向IO扩展驱动
  *          软件I2C(bit-bang, 开漏): PB6=SCL, PB7=SDA
  *
  * 改用软件I2C原因: STM32F4硬件I2C时序兼容性差, 本工程硬件I2C扫描
  * 0x20/0x38均无应答; 软件I2C参数与本工程matrix_keyboard成功经验一致
  * (约30kHz, 位延时17us, ACK建立延时50us, 开漏+外部4.7k上拉,
  *  开漏模式下写1即释放总线, 无需切换GPIO方向)
  *
  * 引脚与接线与硬件I2C版本完全相同, 无需改动任何外部接线
  ******************************************************************************
  */
#include "pcf8574.h"
#include "delay.h"

/* 软件I2C时序参数(与matrix_keyboard一致, 约30kHz) */
#define I2C_BIT_DELAY_US     17u     /* 半位周期, SCL高/低电平时间 */
#define I2C_ACK_SETTLE_US    50u     /* ACK时钟前SDA释放建立时间 */

/* 实际使用的7位地址, 默认0x20, 扫描到0x38则切换 */
static uint8_t pcf_addr = PCF8574_ADDR;

/* ---------------- 开漏引脚操作: 写1=释放(外部上拉拉高), 写0=拉低 ---------------- */
#define SCL_HIGH()   GPIO_WriteBit(GPIOB, GPIO_Pin_6, Bit_SET)
#define SCL_LOW()    GPIO_WriteBit(GPIOB, GPIO_Pin_6, Bit_RESET)
#define SDA_HIGH()   GPIO_WriteBit(GPIOB, GPIO_Pin_7, Bit_SET)
#define SDA_LOW()    GPIO_WriteBit(GPIOB, GPIO_Pin_7, Bit_RESET)
#define SDA_READ()   GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_7)
#define SCL_READ()   GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_6)

static void I2C_Delay(void)
{
    delay_us(I2C_BIT_DELAY_US);
}

/* 主机发送起始: SCL高电平时SDA由高变低 */
static void I2C_Start(void)
{
    SDA_HIGH();
    SCL_HIGH();
    I2C_Delay();
    SDA_LOW();
    I2C_Delay();
    SCL_LOW();
    I2C_Delay();
}

/* 主机发送停止: SCL高电平时SDA由低变高 */
static void I2C_Stop(void)
{
    SDA_LOW();
    SCL_HIGH();
    I2C_Delay();
    SDA_HIGH();
    I2C_Delay();
}

/* 主机发送1字节, 返回从机ACK: 0=ACK(有设备), 1=NACK(无设备) */
static uint8_t I2C_WriteByteRaw(uint8_t data)
{
    uint8_t i;
    uint8_t ack;

    for (i = 0; i < 8; i++)
    {
        /* SCL低电平期间改变SDA */
        if (data & 0x80) SDA_HIGH();
        else             SDA_LOW();
        data <<= 1;
        I2C_Delay();
        SCL_HIGH();                 /* 从机在SCL高电平采样 */
        I2C_Delay();
        SCL_LOW();
    }

    /* 第9个时钟读ACK */
    SDA_HIGH();                     /* 主机释放SDA */
    delay_us(I2C_ACK_SETTLE_US);    /* 建立时间, 等从机拉低 */
    SCL_HIGH();
    I2C_Delay();
    ack = (SDA_READ() == Bit_RESET) ? 0 : 1;   /* 低=ACK */
    SCL_LOW();
    I2C_Delay();
    return ack;
}

/* 主机读1字节. ack=0发ACK(继续读), ack=1发NACK(读完) */
static uint8_t I2C_ReadByteRaw(uint8_t ack)
{
    uint8_t i;
    uint8_t data = 0;

    SDA_HIGH();                     /* 主机释放SDA, 交给从机驱动 */
    for (i = 0; i < 8; i++)
    {
        SCL_HIGH();
        I2C_Delay();
        data <<= 1;
        if (SDA_READ() == Bit_SET) data |= 0x01;
        SCL_LOW();
        I2C_Delay();
    }

    /* 主机发送ACK/NACK */
    if (ack) SDA_HIGH();            /* 非应答 */
    else     SDA_LOW();             /* 应答 */
    I2C_Delay();
    SCL_HIGH();
    I2C_Delay();
    SCL_LOW();
    SDA_HIGH();                     /* 释放SDA */
    I2C_Delay();
    return data;
}

static uint8_t PCF8574_Probe(uint8_t addr7);
/* ===================== 全地址扫描(0x01~0x7E) ===================== */
uint8_t PCF8574_ScanBus(uint8_t *addr_list, uint8_t max_count)
{
    uint8_t a;
    uint8_t n = 0;
    for (a = 0x01; a <= 0x7E; a++)
    {
        /* 跳过I2C保留地址段 0000xxx/1111xxx(0x00-0x07,0x78-0x7F) */
        if (a <= 0x07 || a >= 0x78) continue;
        if (PCF8574_Probe(a))
        {
            if (n < max_count) addr_list[n] = a;
            n++;
        }
    }
    return n;
}

/* 探测7位地址: 1=设备存在(ACK), 0=无设备 */
static uint8_t PCF8574_Probe(uint8_t addr7)
{
    uint8_t ack;
    I2C_Start();
    ack = I2C_WriteByteRaw((uint8_t)(addr7 << 1));   /* 写操作寻址 */
    I2C_Stop();
    return (ack == 0) ? 1 : 0;
}

/* ===================== GPIO初始化 + 地址扫描 ===================== */
uint8_t PCF8574_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    uint8_t found_addr = 0;

    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOB, ENABLE);
    /* 不开硬件I2C1时钟, 纯软件模拟 */

    /* PB6=SCL, PB7=SDA, 开漏输出; 写1时释放总线由外部4.7kΩ上拉,
     * 同时可读输入寄存器获取实际电平, 无需切换输入/输出方向 */
    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_6 | GPIO_Pin_7;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_OUT;
    GPIO_InitStructure.GPIO_OType = GPIO_OType_OD;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_PuPd  = GPIO_PuPd_UP;   /* 内部上拉+外部4.7kΩ */
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    /* 总线空闲态 */
    SCL_HIGH();
    SDA_HIGH();
    delay_us(50);

    /* 全地址扫描: 优先0x20, 其次0x38, 再次任意应答地址(兼容拨码方向差异) */
    {
        uint8_t list[16];
        uint8_t n = PCF8574_ScanBus(list, 16);
        uint8_t k;
        for (k = 0; k < n; k++)
        {
            if (list[k] == PCF8574_ADDR)
            {
                pcf_addr = PCF8574_ADDR;
                found_addr = PCF8574_ADDR;
                break;
            }
        }
        if (found_addr == 0)
        {
            for (k = 0; k < n; k++)
            {
                if (list[k] == PCF8574A_ADDR)
                {
                    pcf_addr = PCF8574A_ADDR;
                    found_addr = PCF8574A_ADDR;
                    break;
                }
            }
        }
        if (found_addr == 0 && n > 0)
        {
            /* 扫到设备但不是0x20/0x38: 直接用第一个应答地址(多为拨码状态不同) */
            pcf_addr = list[0];
            found_addr = list[0];
        }
    }

    /* 无论扫没扫到都发一次安全电平(失败由上层日志体现) */
    PCF8574_WriteByte(PCF8574_SAFE_OUTPUT);
    return found_addr;
}

uint8_t PCF8574_GetAddr(void)
{
    return pcf_addr;
}

/* ===================== 总线物理层自检(不依赖从机) ===================== */
void PCF8574_BusDiag(PCF_BusDiag_t *d)
{
    /* 1) 空闲电平: 两线释放, 给足时间让上拉(含内部弱上拉)充电 */
    SCL_HIGH();
    SDA_HIGH();
    delay_ms(2);
    d->scl_idle = (SCL_READ() == Bit_SET) ? 1 : 0;
    d->sda_idle = (SDA_READ() == Bit_SET) ? 1 : 0;

    /* 2) 主动拉低回读: 验证MCU引脚能驱动且线确实连到测试点 */
    SCL_LOW();
    SDA_LOW();
    delay_ms(1);
    d->scl_pulldown_ok = (SCL_READ() == Bit_RESET) ? 1 : 0;
    d->sda_pulldown_ok = (SDA_READ() == Bit_RESET) ? 1 : 0;

    /* 3) 释放回读: 验证上拉通路(外部4.7k或内部上拉)能把线拉高 */
    SCL_HIGH();
    SDA_HIGH();
    delay_ms(2);
    d->scl_release_ok = (SCL_READ() == Bit_SET) ? 1 : 0;
    d->sda_release_ok = (SDA_READ() == Bit_SET) ? 1 : 0;
}

/* ===================== 写1字节 ===================== */
uint8_t PCF8574_WriteByte(uint8_t data)
{
    uint8_t err = 0;

    I2C_Start();
    if (I2C_WriteByteRaw((uint8_t)(pcf_addr << 1))) { err = 1; goto out; }  /* 地址ACK */
    if (I2C_WriteByteRaw(data))                     { err = 1; goto out; }  /* 数据ACK */

out:
    I2C_Stop();
    return err;
}

/* ===================== 读1字节(NACK后STOP) ===================== */
uint8_t PCF8574_ReadByte(uint8_t *val)
{
    uint8_t err = 0;

    *val = 0xFF;
    I2C_Start();
    if (I2C_WriteByteRaw((uint8_t)((pcf_addr << 1) | 0x01))) { err = 1; goto out; }
    *val = I2C_ReadByteRaw(1);    /* 只读1字节, 发NACK */

out:
    I2C_Stop();
    return err;
}
