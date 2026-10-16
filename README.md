# 智能快递柜系统（Smart Parcel Locker）

STM32F407VE 主控的 4 格口智能快递柜。支持 **触摸屏 / 语音** 双通道投件取件、
离线取件码掉电持久化、AI 语音对话与语音播报，并通过 ESP8266 与本地服务器保持
长连接，由 Web 后台实时监控格口状态、远程开锁和写码。

本仓库是**作品集封装版**：把原来 2265 行的单文件 `main.c` 按职责拆成 7 个应用模块，
固件与云端代码各自成包、可独立编译运行。
演示视频体积较大，放在 [Releases](../../releases) 附件中，不纳入源码仓库。

---

## 一、功能概览

| 功能 | 说明 |
|---|---|
| 投件 | 输入手机号后 6 位 → 自动分配第一个空闲格口 → 生成 6 位取件码 → 开门 |
| 取件 | 输入 6 位取件码 → 匹配格口 → 开门 |
| 语音投取件 | LD3320 语音识别 6 位码，支持逐位播报回读、"对/错"确认与撤销 |
| AI 对话 | 在线走云端大模型，离线走本地关键词表（`app_voice.c` 的 `s_chat_table`） |
| 格口状态 | PCF8574 P4-P7 门磁反馈，每格口独立跟踪"开门→关门"全过程 |
| 云端 | TCP 长连接上报 `STATUS`，接收 `OPEN/UNLOCK/SET/CLEAR/AI:` 下行指令 |
| Web 后台 | 格口占用、取件码、远程开锁、投件超时自动作废（`cloud/`） |
| 时钟天气 | 无 RTC，借 HTTP `Date:` 头校准 + DWT 周期计数器守时 |

---

## 二、硬件构成

| 模块 | 型号 / 接口 | 接脚 |
|---|---|---|
| 主控 | STM32F407VE，168MHz | — |
| 显示屏 | ST7796S + XPT2046（软件 SPI） | LCD: CS=PA4 DC=PB0 RST=PB1 BLK=PB2 SCK=PA5 MISO=PA6 MOSI=PA7<br>Touch: T_CS=PA0 T_IRQ=PA1 T_CLK=PE7 T_DIN=PE8 T_DO=PE9 |
| IO 扩展 | PCF8574（I2C1，4.7k 上拉） | PB6/PB7，地址开机自动扫描；P4-P7 作门磁输入 |
| 语音合成 | SYN6288（UART4，9600，只发） | PC10 |
| 语音识别 | LD3320 + STC11（USART2，9600） | PA2/PA3 |
| 电磁锁 | 4 路继电器直驱（低电平有效） | PB8/PB9/PB12/PB13，导通 2000ms 自动断电 |
| 串口调试 | USART1 | PA9，115200 |
| 联网 | ESP8266（AT 固件，USART3） | 连服务器 TCP 8090 |

---

## 三、目录结构

```
smart-parcel-locker/
├── README.md                  ← 本文件
├── firmware/                  ← STM32 固件（Keil MDK 工程，自包含可独立编译）
│   ├── locker.uvprojx         Keil MDK 工程文件
│   ├── app/                   ← 应用层（本次拆分出来的部分）
│   │   ├── app_config.h          全局常量、状态机枚举、UI 布局宏
│   │   ├── app_ctx.c/.h          跨模块共享状态（原 file-scope static）
│   │   ├── app_ui.c/.h           标题栏、时钟/天气卡片、菜单、浮层、状态栏
│   │   ├── app_clock.c/.h        无 RTC 走时 + 天气抓取
│   │   ├── app_door.c/.h         继电器驱动 + 门磁状态跟踪
│   │   ├── app_voice.c/.h        LD3320 语音前端 + AI 对话
│   │   ├── app_cloud.c/.h        TCP 云端链路（HELLO/STATUS/OPEN…）
│   │   ├── app_flow.c/.h         投件/取件/管理模式状态机
│   │   └── main.c                开机初始化 + ~5ms 主循环（仅剩 477 行）
│   ├── driver/                ← 自己写的外设驱动
│   │   ├── pcf8574.c/.h          IO 扩展 + 门磁读取
│   │   ├── lock.c/.h             电磁锁驱动（含过热保护）
│   │   ├── codebox.c/.h          取件码表 Flash 持久化
│   │   ├── keypad.c/.h           6 位数字键盘控件
│   │   └── esp8266.c/.h          非阻塞 AT 状态机 + 借链路 HTTP GET
│   ├── bsp/                   ← 复用/移植的底层代码
│   │   ├── tft_lcd/              lcd / touch / font / button / hzfont
│   │   ├── ld3320/               语音识别
│   │   ├── syn6288/              语音合成
│   │   ├── delay/  usart/        延时与串口（公共库）
│   └── RTE/                   ← CMSIS 启动文件与器件配置
└── cloud/                     ← 服务器端（Python / FastAPI）
    ├── server.py                 TCP 柜机服务(8090) + Web API + SQLite
    ├── static/index.html         Web 管理后台
    ├── ai_chat.py                大模型对话
    ├── notifier.py               通知
    ├── config.example.json      配置模板（复制为 config.json 后填自己的密钥）
    ├── requirements.txt / start.bat
    └── README.md                 云端组件说明
```

---

## 四、模块拆分结果

拆分的**唯一原则是"只搬代码、不改逻辑"**：所有函数体逐字节取自原 `main.c`，
仅把跨模块调用的函数去掉 `static` 并在头文件声明，把跨模块的状态集中到 `app_ctx.c`。

| 文件 | 行数 | 职责 | 对外接口 |
|---|---:|---|---|
| `app_config.h` | 91 | 全局常量、`ST_*` 枚举、UI 布局宏 | — |
| `app_ctx.c/.h` | 51 / 63 | 跨模块共享状态（原全局变量） | 变量 `extern` |
| `app_ui.c/.h` | 361 / 34 | 界面绘制与状态栏 | `StatusBar_Set` / `Menu_Draw*` / `*Done_Draw` / `PickBtn_*` |
| `app_clock.c/.h` | 391 / 21 | 走时与天气 | `Clock_DrawCard` / `Wx_DrawCard` / `Clock_Weather_Tick` |
| `app_door.c/.h` | 191 / 22 | 开锁与门磁跟踪 | `DoOpenBox` / `Door_Tick` / `DoorStatus_*` |
| `app_voice.c/.h` | 299 / 29 | 语音识别与对话 | `Voice_Handle*` / `Chat_Handle*` / `Voice_Touch*` |
| `app_cloud.c/.h` | 156 / 23 | 云端链路 | `Cloud_Tick` / `Cloud_HandleLine` / `Cloud_Queue` |
| `app_flow.c/.h` | 282 / 24 | 投取件状态机 | `State_*` / `Kpad_*` |
| `main.c` | **477** | 开机初始化、主循环、触摸消抖、状态分发 | `main()` |
| **合计** | **2515** | 原 2265 行 → 8 个文件 + 7 个头文件 | |

拆分后模块之间是**双向调用**的（界面里要拉业务数据，业务又要刷新界面），
例如 `app_ui.c` 调 `Clock_DrawCard()` 画时钟，而 `app_clock.c` 抓到新时间后又要
回调 `TitleBar_DrawLink()` 刷新标题栏的联网角标。

这里没有用"分层单向依赖"强行拆，因为那会把一个真实的界面刷新动作来回绕三层；
取而代之的约束更简单也更有效：

> **头文件之间从不互相 `#include`**，每个 `.h` 只包含 `app_config.h` / `app_ctx.h`。
> 双向调用只发生在 `.c` 文件里（各自 include 对方的功能头文件），
> 因此不可能出现头文件循环包含导致的编译错误。

```
app_config.h ──> 被所有模块包含（只放常量/枚举/宏）
app_ctx.h    ──> 被所有模块包含（只放共享变量 extern）

双向调用（仅存在于 .c 中）：
  main   <->  flow / ui / voice / door / cloud / clock
  flow   <->  ui / voice / door
  voice  <->  ui / door
  cloud  <->  ui / voice / door
  ui     <->  clock / door
```

> 说明：`app_ui.c` 只暴露 `StatusBar_Set()`、`Menu_Draw*()` 等真正的跨模块入口；
> `StatusBar_Draw()`、`TitleBar_Draw()`、`Clock_DrawTime()` 这类只被本模块使用的函数
> 仍保留 `static`，`s_last_msg` / `s_clk_*` / `s_wx_*` / `s_cloud_*` / `s_door_band_*`
> 也留在各自模块内，没有为了"看起来整齐"而无谓地扩大共享面。

---

## 五、编译与烧录

**环境**：Keil MDK-ARM v5.14 + `Keil.STM32F4xx_DFP 1.0.8`（C99，`USE_STDPERIPH_DRIVER`）

```powershell
# 命令行编译
D:\keil\UV4\UV4.exe -b firmware\locker.uvprojx -j0 -o build.log
```

当前编译结果：

```
Program Size: Code=37668  RO-data=11468  RW-data=484  ZI-data=7540
".\Objects\locker.axf" - 0 Error(s), 0 Warning(s)
```

产物 `firmware\Objects\locker.hex` 用 J-Link / ST-Link 烧录即可。

**服务器端**：

```powershell
cd cloud
pip install -r requirements.txt
python server.py            # 或直接双击 start.bat
```

柜机与服务器需在同一网段，`esp8266.c` 中配置 WiFi SSID/密码与服务器 IP，端口 **8090**。
浏览器打开 `http://<服务器IP>:8000` 进入 Web 后台。

---

## 六、关键问题与解决方式

以下都是项目过程中真实踩到的坑，记录现象、根因与最终做法。

### 1. 中断服务函数里调 `printf` 导致串口假死

- **现象**：加了调试打印后，主循环偶发卡住、USART1 再无输出。
- **根因**：`fputc` 重定向是**阻塞**的（等 TXE 标志）。在 `USART1_IRQHandler` 里打印，
  中断被拉长到毫秒级；同时 ORE（溢出）标志没清，会反复重入中断，主循环再也拿不到 CPU。
- **解决**：ISR 里只做"读 DR + 清 ORE"，绝不调用任何阻塞函数；`printf` 只允许主循环用。
- **代码**：[usart.c](file:///d:/STM32/demo/locker/portfolio/firmware/bsp/usart/usart.c#L65-L75)

```c
void USART1_IRQHandler(void)
{
    if (USART_GetITStatus(USART1, USART_IT_ORE) != RESET) { /* 只清标志，不打印 */ }
    /* ...只搬运数据，不做任何延时/输出... */
}
```

### 2. 多个格口同时动作会"串柜"

- **现象**：投件之后门磁状态错乱，A 格开的门被记到 B 格上。
- **根因**：最初只用**一组全局变量**记录"当前正在跟踪的门"。第二个流程一启动就把它覆盖了。
- **解决**：每格口一份独立状态机，`Door_Tick()` 用 `for (box = 1..BOX_COUNT)` 逐格推进，
  格口之间互不影响。
- **代码**：[app_ctx.h](file:///d:/STM32/demo/locker/portfolio/firmware/app/app_ctx.h#L48-L52)、
  [app_door.c](file:///d:/STM32/demo/locker/portfolio/firmware/app/app_door.c#L102)

```c
extern uint8_t  s_door_phase[BOX_COUNT];     /* IDLE / W_OPEN / W_CLOSE / DONE */
extern uint8_t  s_door_flow[BOX_COUNT];      /* 1 = 投件, 2 = 取件 */
extern uint8_t  s_door_db[BOX_COUNT];        /* 稳定读数计数（消抖） */
extern uint32_t s_door_open_tick[BOX_COUNT]; /* 开门时刻，用于超时兜底 */
```

### 3. 上电自检把全部柜门打开（安全缺陷）

- **现象**：早期版本一上电，四个柜门继电器全部吸合，柜门全弹开。
- **根因**：把"复位到关锁态"和"开锁"用了同一个写值，自检时误当成开锁。
- **解决**：上电只做 **强制关锁 + 读门磁上报诊断**，任何情况下都不主动下发开锁电平；
  开锁必须显式调用 `Lock_Open()` 并且只对目标格口生效。
- **代码**：启动自检段 [main.c](file:///d:/STM32/demo/locker/portfolio/firmware/app/main.c)、
  [lock.c](file:///d:/STM32/demo/locker/portfolio/firmware/driver/lock.c)

### 4. 电磁锁线圈过热 / 连点把计时器重置

- **现象**：连续点按开锁后锁体烫手；电磁锁长时间通电会烧线圈。
- **根因**：初版用 `delay_ms(2000)` 堵在主循环里断电，既卡界面，也让"连点"有机会重新计时。
- **解决**（四道防线）：
  1. `LOCK_ON_TIME_MS = 2000`，由 `Lock_Tick()` **非阻塞**到点自动断电，主循环不阻塞；
  2. 超时判据用 **DWT 周期计数**而不是累加 tick，连点无法重置计时；
  3. 编译期硬约束 —— 阈值一旦改到 ≥3000ms 直接编译失败，把"线圈通电极限"写进编译规则；
  4. PCF8574 写失败重试 3 次。
- **代码**：[lock.h](file:///d:/STM32/demo/locker/portfolio/firmware/driver/lock.h#L26-L28)、
  [lock.c](file:///d:/STM32/demo/locker/portfolio/firmware/driver/lock.c#L208)

```c
#define LOCK_ON_TIME_MS       2000u
#if LOCK_ON_TIME_MS >= 3000u
#error "LOCK_ON_TIME_MS must be < 3000ms to protect the solenoid coil!"
#endif
```

### 5. 掉电导致取件码表变成脏数据

- **现象**：运行中直接断电，重启后取件码表出现乱码/重复码，取件会开错柜门。
- **根因**：整表"擦除 → 逐字节写入"期间掉电，Flash 镜像停在半写状态；
  若只按固定偏移读，会读到上一轮的残留数据。
- **解决**：
  1. 镜像头 4 字节写 `CODEBOX_MAGIC (0xCFB88125)`，读取先校验魔数，不匹配就**整体零初始化重建**；
  2. 整表在 RAM 里组装完再一次性擦 + 写；**擦除失败时保留 RAM 副本**，不清零；
  3. 掉电丢码仍可用手机号后 6 位取件（`CodeBox_PickupByPhone`）兜底，功能不至于不可用。
- **代码**：[codebox.h](file:///d:/STM32/demo/locker/portfolio/firmware/driver/codebox.h#L34)、
  [codebox.c](file:///d:/STM32/demo/locker/portfolio/firmware/driver/codebox.c#L65-L95)

### 6. ESP8266 断线后主循环直接死锁

- **现象**：路由器重启或信号丢失后柜机黑屏卡死，必须手动断电。
- **根因**：早期用阻塞式 `while (!ESP8266_IsReady())` 等 AT 握手，超时路径没有出口。
- **解决**：改成**非阻塞 AT 状态机** `esp_state_t`
  （`ES_INIT → ES_CWJAP → ES_CIPSTART → ES_CONNECTED`），每 5ms tick 推进一步；
  `CWJAP` 最多重试 5 次，`CIPSTART` 分层重试（首连与重试各 2000 tick 稳定期）。
  全程没有任何阻塞等待，断网时界面照常可触摸操作。
- **关键约束**：USART 接收 ISR **不修改** `s_state` / `s_esp_sent`，
  因此不可能出现"AT 命令被双发"这种只在中断与主循环竞争时才出现的偶发 bug。
- **代码**：[esp8266.c](file:///d:/STM32/demo/locker/portfolio/firmware/driver/esp8266.c#L25-L60)

### 7. 开门流程卡死，界面永远停在"DONE"

- **现象**：用户开了门没关就走，界面停在完成页，格口状态永远不落定。
- **解决**：`PICKUP_TIMEOUT_TICKS = 6000`（≈30s）超时兜底，
  超时后按"已取件"处理，上报 `PICKED n` 并回主菜单，避免格口被永久占用。
- **代码**：[app_config.h](file:///d:/STM32/demo/locker/portfolio/firmware/app/app_config.h#L44)、
  [app_door.c](file:///d:/STM32/demo/locker/portfolio/firmware/app/app_door.c)

### 8. 云端 `depositing` 记录卡住，格口一直被占

- **现象**：柜机上报"投件中"后掉线，后台该格口一直显示占用，新投件分配不到格子。
- **解决**：
  1. 后台定时任务 `expire_stale_deposits()`，`DEPOSIT_TIMEOUT_SEC = 600`（10 分钟）
     后删除超时的 `depositing` 记录并释放格口，同时打日志便于排查；
  2. 反向的坑：柜机抓天气时会**主动断开**中继链路几秒，后台不能立刻判"离线"，
     于是加 `LOCKER_GRACE_SEC = 90`，用最近上报时间兜住短暂断开。
- **代码**：[server.py](file:///d:/STM32/demo/locker/portfolio/cloud/server.py#L53)、
  [server.py](file:///d:/STM32/demo/locker/portfolio/cloud/server.py#L133)

### 9. 继电器动作瞬间干扰 LCD 和门磁读取

- **现象**：开锁瞬间屏幕花屏；同一时刻 PCF8574 门磁读回 `2`（I2C 失败）。
- **根因**：继电器吸合瞬间 5V 轨塌陷 + EMI，LCD 控制器和 I2C 总线都被带偏。
- **解决**：`DoOpenBox()` 先关背光 → `delay_ms(50)` → 开锁 → `delay_ms(80)`
  等触点和 5V 稳定 → `LCD_Init()` 复位 LCD 控制器；
  门磁读回 `2` 时按"读数无效"跳过，**不当作门开**，避免误判。
- **代码**：[app_door.c](file:///d:/STM32/demo/locker/portfolio/firmware/app/app_door.c)

### 10. 没有 RTC 也要显示准确时间

- **解决**：借 ESP8266 的 TCP 链路做一次 HTTP GET，从响应头 `Date:`（GMT）解析日期时间，
  **+8 换算北京时间**后写入软件时钟；之后由**自由运行的 DWT 周期计数器（168MHz）**累加守时。
  日期规范化用 Howard Hinnant 的儒略日算法，跨月、跨年、闰年都不需要写特例分支。
  天气则同一份响应里用轻量 JSON 取值，并把 UTF-8 的风向词查表转成 GBK 再上屏。
- **代码**：[app_clock.c](file:///d:/STM32/demo/locker/portfolio/firmware/app/app_clock.c)

### 11. 触摸抖动造成误触发

- **解决**：对触摸点做**连续采样消抖**（按下/抬起各自计数达阈值才认一次动作）；
  "长按返回主菜单"用独立的按住时长 `td_held` 判断，避免和点击互相干扰。
- **代码**：[main.c](file:///d:/STM32/demo/locker/portfolio/firmware/app/main.c)

### 12. 复位原因不明，偶发重启无法定位

- **解决**：开机读 `RCC->CSR`，把 `POR/BOR/PAD/SFT/WDG/WWDG/LPWR` 各标志位打印出来，
  再写 `RCC_CSR_RMVF` 清除。这样"看门狗复位"和"上电复位"一眼可分，
  排查供电跌落（BOR）和程序跑飞（WWDG）时非常关键。
- **代码**：[main.c](file:///d:/STM32/demo/locker/portfolio/firmware/app/main.c#L79-L89)

### 13. 语音重复识别与"自我触发"

- **现象**：同一串取件码短时间被识别两次，柜门开了两次；
  更隐蔽的是 TTS 播报的数字被麦克风听回去，形成自激。
- **解决**：
  1. `VOICE_DEDUP_TICKS = 1600`（8s）内相同的 6 位码直接丢弃，
     覆盖 STC11 子 MCU 的重试与 TTS 回授窗口；
  2. 用 `s_voice_seen` 区分"真的收到语音字节"和"只是线路噪声"，
     没收到过字节时不弹语音提示；
  3. 对话模式给 `CHAT_IDLE_TICKS = 5000`（25s）租期，
     必须覆盖"STC11 唤醒后 5s 失聪 + 9.6s 聆听窗口 + 反应时间"。
- **代码**：[app_config.h](file:///d:/STM32/demo/locker/portfolio/firmware/app/app_config.h#L48-L50)、
  [app_voice.c](file:///d:/STM32/demo/locker/portfolio/firmware/app/app_voice.c)

### 14. 语音与触摸两条入口逻辑走偏

- **现象**：语音说"对"和触摸点"确认"两条路径各写一遍，改了一处忘了另一处，行为不一致。
- **解决**：把共用动作抽成 `Kpad_AnnounceConfirm()` / `Kpad_DoPickup()`，
  触摸和语音都调用同一份实现，从根上消除两处逻辑漂移。
- **代码**：[app_flow.c](file:///d:/STM32/demo/locker/portfolio/firmware/app/app_flow.c)

### 15. 源码编码：GBK 与 UTF-8 混用导致中文乱码

- **现象**：串口 `printf` 和 LCD 汉字显示乱码，Keil 里注释变成问号。
- **根因**：工程源码是 **GBK**，而编辑器/脚本按 UTF-8 读写会造成二次编码；
  一旦某个文件被"顺手转成 UTF-8"，就只有那一个文件乱码，非常难定位。
- **解决**：所有固件源码统一 **GBK + CRLF**；批量处理脚本一律显式
  `decode('gbk')` / `encode('gbk')`，不做隐式转换。
- **代码**：`firmware/` 下全部源码

---

## 七、架构选型：为什么不用 FreeRTOS

本项目是**前后台架构**（单 `while(1)` + 非阻塞状态机），没有引入 RTOS，理由是：

1. **业务天然是 I/O 事件驱动**：触摸、语音、串口、门磁都是低频事件，
   用状态机每 5ms 轮询一次已经足够，没有需要"抢占"的实时任务；
2. **实时性要求宽松**：最快的响应要求是电磁锁 2000ms 断电，没有任何微秒级硬实时约束；
3. **资源账**：STM32F407VE 有 192KB RAM / 512KB Flash，本工程 ZI + RW 仅约 8KB。
   引入 RTOS 要额外付出 3~8KB RAM 和 6~10KB Flash，还要承担栈溢出、
   优先级反转、临界区保护等一整套新问题——收益为负；
4. **可维护性**：前后台 + 状态机的执行流是确定性的，出了问题看一遍代码就能复现；
   RTOS 的时序 bug 往往需要在线调试器抓现场。

**什么时候该换 RTOS**：当出现多路高速同步采样（如多通道 ADC + DMA 联动）、
需要阻塞式网络协议栈、或者任务之间需要真正的优先级抢占时，
再引入 FreeRTOS 才是划算的。当前形态下不引入是经过权衡的**技术选型**，不是能力缺失。

---

## 八、拆分原则与保真度

| | 拆分前 | 本仓库 |
|---|---|---|
| `main.c` | 2265 行单文件 | 477 行 + 7 个应用模块 |
| 函数体 | — | 逐字节取自原 `main.c`，未做任何逻辑改动 |
| 编译 | — | 自包含，可独立编译（已实测 0 error / 0 warning） |

拆分只做两件事：把跨模块调用的函数去掉 `static` 并在头文件声明；
把原本 file-scope 的共享状态集中到 `app_ctx.c`。除此之外，两份源码逻辑等价。