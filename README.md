# 智能快递柜系统（Smart Parcel Locker）

STM32F407VE 主控的 4 格口智能快递柜。支持 **触摸屏 / 语音** 双通道投件取件、
离线取件码掉电持久化、AI 语音对话与语音播报，并通过 ESP8266 与本地服务器保持
长连接，由 Web 后台实时监控格口状态、远程开锁和写码。

本仓库是**作品集封装版**：把原来 2265 行的单文件 `main.c` 按职责拆成 7 个应用模块，
再在其上**接入 FreeRTOS V202212.01**，把"每 5ms 轮询一次"的超级循环升级为
**7 个任务 + 队列/互斥的事件驱动架构**（详见第七节）。
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
├── firmware/                  ← STM32 固件（Keil MDK 工程，可独立编译）
│   ├── locker.uvprojx         Keil MDK 工程文件
│   ├── app/                   ← 应用层（拆分 + RTOS 化，本次升级的主体）
│   │   ├── app_config.h          全局常量、状态机枚举、UI 布局宏、任务/时序参数（ms）
│   │   ├── app_rtos.c/.h         事件与 IPC 对象、7 个任务的创建、诊断任务
│   │   ├── app_ui_task.c         UI 任务：事件分发 + 截止时间（唯一主要 LCD 写者）
│   │   ├── app_link.c/.h         ESP8266 链路任务（AT 状态机 + 上下行 + 借链路抓天气）
│   │   ├── app_ctx.c/.h          跨模块共享状态（原 file-scope static）
│   │   ├── app_ui.c/.h           标题栏、时钟/天气卡片、菜单、浮层、状态栏
│   │   ├── app_clock.c/.h        时钟任务：无 RTC 走时 + 天气抓取
│   │   ├── app_door.c/.h         门控任务：继电器 + PCF8574 门磁跟踪
│   │   ├── app_voice.c/.h        语音任务（LD3320 前端）+ AI 对话
│   │   ├── app_cloud.c/.h        TCP 云端链路（HELLO/STATUS/OPEN…）
│   │   ├── app_flow.c/.h         投件/取件/管理模式状态机
│   │   ├── hooks.c/.h            断言/栈溢出/堆耗尽/HardFault 现场打印（裸串口）
│   │   ├── FreeRTOSConfig.h      内核配置（tick 1kHz、max prio 10、heap_4、32KB 堆）
│   │   └── main.c                开机初始化 + App_RtosStart() 启动调度器（159 行）
│   ├── freertos/              ← FreeRTOS V202212.01 内核（vendored 进仓库）
│   │   ├── tasks.c / queue.c / list.c
│   │   ├── include/              内核公开头文件
│   │   └── portable/             RVDS/ARM_CM4F 移植层 + MemMang/heap_4.c
│   ├── driver/                ← 自己写的外设驱动
│   │   ├── pcf8574.c/.h          IO 扩展 + 门磁读取
│   │   ├── lock.c/.h             电磁锁驱动（含过热保护）
│   │   ├── codebox.c/.h          取件码表 Flash 持久化
│   │   ├── keypad.c/.h           6 位数字键盘控件
│   │   ├── esp8266.c/.h          非阻塞 AT 状态机 + 借链路 HTTP GET
│   │   └── wifi_config.example.h  本机部署参数模板（复制为 wifi_config.h，已被 .gitignore 忽略）
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

拆分第一步的原则是**"只搬代码、不改逻辑"**：函数体尽量逐字节取自原 `main.c`，
仅把跨模块调用的函数去掉 `static` 并在头文件声明，把跨模块的状态集中到 `app_ctx.c`。
其后的 **FreeRTOS 事件化改造**（第七节）才动了"谁在什么时候跑"这一层。
下表是升级完成后的现状：

| 文件 | 行数 | 职责 | 对外接口 |
|---|---:|---|---|
| `app_config.h` | 99 | 全局常量、`ST_*` 枚举、UI 布局宏、任务/时序参数（ms） | — |
| `app_ctx.c/.h` | 34 / 45 | 跨模块共享状态（原全局变量） | 变量 `extern` |
| `app_rtos.c/.h` | 172 / 102 | 事件结构、IPC 对象、任务创建、诊断任务 | `App_RtosStart` / `App_Ms` / `App_*Post` / `Lcd_Lock` |
| `app_ui_task.c` | 360 | UI 任务：事件分发 + 截止时间 | `Ui_Task` |
| `app_link.c/.h` | 107 / 20 | ESP8266 链路任务：AT 状态机、上下行、借链路抓天气 | `Link_Task` |
| `app_ui.c/.h` | 329 / 31 | 界面绘制与状态栏 | `StatusBar_Set` / `Menu_Draw*` / `*Done_Draw` / `PickBtn_*` |
| `app_clock.c/.h` | 397 / 24 | 时钟任务：走时与天气 | `Clock_Task` / `Clock_DrawCard` / `Wx_DrawCard` |
| `app_door.c/.h` | 270 / 26 | 门控任务：开锁与门磁跟踪 | `Door_Task` / `DoOpenBox` / `DoorStatus_*` |
| `app_voice.c/.h` | 415 / 27 | 语音任务：识别与对话 | `Voice_Task` / `Voice_Handle*` / `Chat_Handle*` |
| `app_cloud.c/.h` | 118 / 20 | 云端链路 | `Cloud_HandleLine` / `Cloud_StatusTick` / `Cloud_Queue` |
| `app_flow.c/.h` | 265 / 20 | 投取件状态机 | `State_*` / `Kpad_*` |
| `hooks.c/.h` | 155 / 25 | 断言 / 栈溢出 / 堆耗尽 / HardFault 现场打印 | `vAssertCalled` / `vHardFaultReport` |
| `FreeRTOSConfig.h` | 78 | 内核配置 | — |
| `main.c` | **159** | 开机初始化、启动调度器 | `main()` |
| **合计（应用层）** | **3298** | 不含 `freertos/` 内核源码 | |

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

**首次构建前**：仓库不含 WiFi 凭证，先复制模板生成本机配置文件（该文件已被 `.gitignore` 忽略，不会提交）：

```powershell
copy firmware\driver\wifi_config.example.h firmware\driver\wifi_config.h
```

再编辑 `firmware\driver\wifi_config.h`，填入 WiFi 名称、密码，以及运行 `cloud/server.py`
那台电脑的 IP（Windows 移动热点通常为 `192.168.137.1`）。

```powershell
# 命令行编译
D:\keil\UV4\UV4.exe -b firmware\locker.uvprojx -j0 -o build.log
```

当前编译结果（接入 FreeRTOS 之后）：

```
Program Size: Code=50008  RO-data=11712  RW-data=648  ZI-data=41624
".\Objects\locker.axf" - 0 Error(s), 0 Warning(s)
```

> 相对接入前的 `Code=37668 / ZI-data=7540`，Flash 增加约 12KB（内核 + 7 个任务 + IPC），
> RAM 增加约 34KB —— 其中 32KB 是 `heap_4` 的静态堆数组 `ucHeap`（`configTOTAL_HEAP_SIZE`），
> 其余是 7 个任务的栈与 TCB。STM32F407VE 的 512KB Flash / 192KB RAM 余量充足。

产物 `firmware\Objects\locker.hex` 用 J-Link / ST-Link 烧录即可。

**服务器端**：

```powershell
cd cloud
pip install -r requirements.txt
python server.py            # 或直接双击 start.bat
```

柜机与服务器需在同一网段，WiFi 名称/密码与服务器 IP 在 `firmware\driver\wifi_config.h` 中配置，端口 **8090**。
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
- **解决**：每格口一份独立状态机，`Door_Advance()` 用 `for (box = 1..BOX_COUNT)` 逐格推进，
  格口之间互不影响；升级到 RTOS 后，这份状态数组由门控任务**独占读写**（见第七节）。
- **代码**：[app_door.c](file:///d:/STM32/demo/locker/portfolio/firmware/app/app_door.c#L150-L203)

```c
static uint8_t  s_phase[BOX_COUNT];   /* 只由 door 任务写 */
static uint8_t  s_flow[BOX_COUNT];    /* 1 = 投件, 2 = 取件, 3 = 远程 */
static uint8_t  s_db[BOX_COUNT];      /* 稳定读数计数（消抖） */
static uint32_t s_open_ms[BOX_COUNT]; /* 开门时刻，用于 30s 超时兜底 */
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
- **解决**：`PICKUP_TIMEOUT_MS = 30000`（30s）超时兜底，超时后按"已取件"处理，
  上报 `PICKED n` 并回主菜单，避免格口被永久占用。判据是 `Door_Advance()` 里的
  tick 截止时间（`App_Ms() - s_open_ms[box]`），不再依赖轮询计数。
- **代码**：[app_config.h](file:///d:/STM32/demo/locker/portfolio/firmware/app/app_config.h)、
  [app_door.c](file:///d:/STM32/demo/locker/portfolio/firmware/app/app_door.c#L165-L174)

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
  升级到 RTOS 后，这段消抖整体搬进了触摸任务 `Touch_Task()`（仍只在按下期间投递事件）。
- **代码**：[touch.c](file:///d:/STM32/demo/locker/portfolio/firmware/bsp/tft_lcd/touch.c)、
  [app_ui_task.c](file:///d:/STM32/demo/locker/portfolio/firmware/app/app_ui_task.c)

### 12. 复位原因不明，偶发重启无法定位

- **解决**：开机读 `RCC->CSR`，把 `POR/BOR/PAD/SFT/WDG/WWDG/LPWR` 各标志位打印出来，
  再写 `RCC_CSR_RMVF` 清除。这样"看门狗复位"和"上电复位"一眼可分，
  排查供电跌落（BOR）和程序跑飞（WWDG）时非常关键。
- **代码**：[main.c](file:///d:/STM32/demo/locker/portfolio/firmware/app/main.c#L88-L100)

### 13. 语音重复识别与"自我触发"

- **现象**：同一串取件码短时间被识别两次，柜门开了两次；
  更隐蔽的是 TTS 播报的数字被麦克风听回去，形成自激。
- **解决**：
  1. `VOICE_DEDUP_MS = 8000`（8s）内相同的 6 位码直接丢弃，
     覆盖 STC11 子 MCU 的重试与 TTS 回授窗口；
  2. 用 `s_voice_seen` 区分"真的收到语音字节"和"只是线路噪声"，
     没收到过字节时不弹语音提示（该标志由语音任务置位）；
  3. 对话模式给 `CHAT_IDLE_MS = 25000`（25s）租期，
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

### 16. 抓天气时 AT 命令被当成 TCP 载荷发给了服务器

- **现象**：每 ~180s 抓一次天气，柜机与服务器的长连接就断一次，而且几十秒回不来，
  Web 后台这段时间只能显示"柜机离线"；服务器日志里还夹着一条
  `未识别指令: 'AT+CIPCLOSE'` 和 `[WinError 10054] 远程主机强迫关闭了一个现有的连接`。
- **根因**：`AT+CIPMUX=0` 只有一个 socket，抓天气的第一步必须用 `AT+CIPCLOSE`
  把中继链路腾出来，这是设计如此。但 `ESP8266_FetchStart()` 当时只检查了
  `ESP8266_IsReady()`——而该函数在 `ES_SEND_PROMPT` / `ES_SEND_DATA`
  （模块正等 `AT+CIPSEND` 声明的载荷字节）时**同样返回 1**。于是上行 `STATUS`
  发到一半时借走链路，`AT+CIPCLOSE` 这串字符被模块当成 TCP 载荷吐给了服务器，
  模块随即进入混乱态：之后 `AT+CIPSTART` 连续返回 `ERROR`（重试 3/4/5 次），
  直到 `[ESP] retry handshake` 重跑整轮握手才恢复。
- **解决**：把仓库里已声明、已实现、却**始终没有任何调用点**的
  `ESP8266_SendIsPending()` 接进 `FetchStart()`：发送未结束时返回"忙"，
  link 任务按既有路径把这个拒绝回报给时钟任务，weather 延后
  `WX_RETRY_MS`（12s）重试。判断**刻意放在 `IsReady()` 之前**——
  `IsReady()` 认得的几种发送态里，只有"正等载荷"这一种会吃掉命令，必须优先拦住。
- **代码**：[esp8266.c](file:///d:/STM32/demo/locker/portfolio/firmware/driver/esp8266.c#L978-L983)、
  [app_link.c](file:///d:/STM32/demo/locker/portfolio/firmware/app/app_link.c#L99-L105)

---

## 七、架构升级：接入 FreeRTOS 的事件驱动改造

最初版本是**前后台架构**（单 `while(1)` + 每 5ms 轮询一次的非阻塞状态机）。
它的执行流确定性好、RAM 占用小，但也意味着"等待某件外部事情发生"只能靠
**在循环里反复检查 + 自己维护超时计数器**——ESP8266 的 AT 应答、60s 的 HTTP
抓天气窗口、25s 的语音对话租期，都得拆成一堆散落的计数变量。

这次升级把内核换成 **FreeRTOS V202212.01**（`freertos/` 目录，随仓库一起 vendor，
不依赖外部 SDK），并把轮询改成**事件驱动**：每个外设由自己的任务负责，
任务之间用队列通信，UI 任务只在收到事件时才重绘。

### 7.1 任务划分

| 任务 | 优先级 | 栈（字） | 职责 | 触发方式 |
|---|---:|---:|---|---|
| `ui` / `Ui_Task` | 5 | 1024 | UI 绘制、状态机、按键分发、云端下行处理 | `q_ui` 事件 + 50ms 截止时间 |
| `touch` / `Touch_Task` | 4 | 256 | 触摸采样与消抖 | 5ms 自节拍 |
| `voice` / `Voice_Task` | 4 | 384 | LD3320 识别结果解析、TTS | 5ms 自节拍 |
| `door` / `Door_Task` | 4 | 384 | 继电器驱动、PCF8574 门磁跟踪 | 20ms 节拍 + `q_door` 请求 |
| `link` / `Link_Task` | 3 | 768 | ESP8266 AT/TCP、上下行、借链路抓天气 | 5ms 自节拍 + `q_link` |
| `clock` / `Clock_Task` | 2 | 320 | 无 RTC 走时、天气卡片 | 100ms 自节拍 + `q_clock` |
| `diag` / `Diag_Task` | 1 | 320 | 堆水位 / 各任务栈高水位 / 锁掩码 | 5s 自节拍 |

优先级数字来自 [app_rtos.c](file:///d:/STM32/demo/locker/portfolio/firmware/app/app_rtos.c#L183-L191)，
`configMAX_PRIORITIES = 10`。UI 最高是因为它是唯一的主要 LCD 写者，
迟一拍就会看到画面撕裂；诊断任务最低，只在大家都空闲时跑。

### 7.2 事件与 IPC

核心事件只有 5 种，定义在 [app_rtos.h](file:///d:/STM32/demo/locker/portfolio/firmware/app/app_rtos.h)：

```
EV_TOUCH        触摸按下/抬起（含坐标，td_held 表示"仍在按住"）
EV_VOICE        LD3320 的一次识别结果（数字/撤销/确认/唤醒/对话/原始行）
EV_CLOUD_LINE   ESP8266 收到的一行下行数据
EV_DOOR_STATE   门磁状态变化（用于重绘门状态行）
EV_DOOR_DONE    一次开门流程结束（超时/投件完成/取件完成/关门）
```

| 对象 | 类型 | 容量 | 生产者 → 消费者 |
|---|---|---|---|
| `q_ui` | 队列 | 8 × `app_evt_t` | touch / voice / link / door → ui |
| `q_link` | 队列 | 4 × `app_link_t` | ui(云端上行) / clock(抓天气) → link |
| `q_clock` | 队列 | 2 × `uint8_t` | link(抓取结果) → clock |
| `q_door` | 队列 | 4 × `uint8_t` | ui / voice / cloud(开箱请求) → door |
| `m_lcd` | 互斥量 | — | ui / clock 之间串行化 LCD 访问 |

所有 **投递都是非阻塞的**（`xQueueSend(..., 0)`）：队列满时丢弃并计数，
绝不允许一个生产者因为消费者慢而卡住。丢包计数由诊断任务打印，
"队列满"在设计上就是 bug，不是正常背压。

`m_lcd` 的获取方式是"**收到事件之后再取锁**"，而不是"持锁等事件"——
UI 任务在主循环里先 `xQueueReceive` 再 `Lcd_Lock()`，避免拿着锁去睡眠而把
另一个 LCD 使用者（时钟任务）饿死。

### 7.3 跨任务只读快照

有些状态被一个任务写、被多个任务读（比如门磁掩码、链路状态）。
这些不走队列，而是用 `volatile` 单字节快照，只保证"读到一个曾经存在过的值"：

| 快照 | 写入者 | 读取者 |
|---|---|---|
| `g_link_up` / `g_link_fetch` / `g_link_status` | link | ui, clock, cloud |
| `g_door_fb` / `g_door_fb_ok` | door | ui, cloud |
| `g_lock_mask` | door | diag |
| `g_ui_screen` / `g_chat_busy` | ui | clock, cloud |
| `g_voice_seen` | voice | ui |

### 7.4 内核与驱动共存的三个硬冲突

接内核不是加几个文件那么简单，有三处**必须改**，否则会静默跑飞：

1. **SysTick 必须归内核独占**。原 `Delay.c` 直接写 `SysTick->LOAD/VAL/CTRL`，
   和内核的 1kHz tick 中断会互相踩。改为使用 **DWT 周期计数器**：
   `Delay_Init()` 打开 DWT，`delay_us` 用 DWT 忙等，
   `delay_ms` 判断调度器是否已启动（未启动忙等、已启动 `vTaskDelay`）。
   全仓库不再有任何一处写 `SysTick`。
2. **NVIC 分组必须是 `NVIC_PriorityGroup_4`**。内核的
   `configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY = 5` 假设
   "数值优先级 = 抢占优先级"。分组用 Group_2 时高 2 位才是抢占位，
   数值和抢占优先级对不上，`FromISR` 版本的 API 会误判可屏蔽范围。
   同时三个串口中断（USART1/2/3）抢占优先级统一为 **6**（≥5），
   这样它们才可以调用带 `FromISR` 的接口。
3. **`printf` 必须串行化**。7 个任务都往 USART1 打印，
   直接并发会输出交错。做法是在 [usart.c](file:///d:/STM32/demo/locker/portfolio/firmware/bsp/usart/usart.c)
   的 `fputc` 里加一把 `m_log` 互斥量（50ms 超时），
   并给诊断/断言路径准备了**不取锁的裸输出层**（`Usart1_RawPuts` 等），
   因为 `configASSERT` 和 HardFault 现场往往发生在锁已经损坏的时候。
   **锁顺序固定为 `m_lcd` → `m_log`**，不允许反向嵌套。

另外，**中断服务函数里不阻塞、不 `printf`**：USART/LD3320/ESP8266 的 ISR
只把收到的字节塞进环形缓冲，解析全部放到任务里做。

### 7.5 时间基准毫秒化

原代码到处是"每轮循环 5ms"的隐含假设，超时常量写成 `xxx_TICKS`。
接入内核后统一改为毫秒：

```c
uint32_t App_Ms(void) { return (uint32_t)xTaskGetTickCount(); }   /* tick = 1kHz */
```

`PICKUP_TIMEOUT_MS`、`UI_TICK_MS`、`DOOR_FB_POLL_MS`、`CHAT_IDLE_MS`…
全部变成 `pdMS_TO_TICKS()` 可直接消费的毫秒值，`loop_tick` 变量整体删除。
UI 任务的"截止时间"（2s 上报状态、25s 聊天租期、3s 标题栏长按）
不再靠轮询计数，而是用 `xQueueReceive` 的 **50ms 超时**来推进。

### 7.6 诊断与失败模式

接入 RTOS 引入了新的崩溃方式（栈溢出、堆耗尽、优先级反转），
所以一并搬来一套**可观测性基建**（[hooks.c](file:///d:/STM32/demo/locker/portfolio/firmware/app/hooks.c)）：

- `configASSERT` → `vAssertCalled()`：打印文件行号后停机；
- `vApplicationStackOverflowHook`：任务名 + 溢出任务，立即停机；
- `vApplicationMallocFailedHook`：堆耗尽；
- `HardFault_Handler`：汇编入口，把**现场寄存器**（R0-R3/R12/LR/PC/xPSR、
  CFSR/HFSR/MMFAR/BFAR）从栈帧里抠出来打印，再停机。

再配合每 5s 一次的诊断任务（堆剩余/历史最低、继电器锁掩码、丢包计数、
每个任务的栈高水位），"多任务跑起来之后仍然健康"就有了自动证据：

```
[DIAG] heap free=27360 min-ever=26408  lock mask=0x0  drops ui/link/door=0/0/0
[DIAG] task ui     stack left=712 words (of 1024)
[DIAG] task touch  stack left=214 words (of 256)
...
```

### 7.7 代价

| | 接入前 | 接入后 | 增量 |
|---|---:|---:|---:|
| Code | 37668 | 50008 | +12.3 KB |
| RO-data | 11468 | 11712 | +0.2 KB |
| RW-data | 484 | 648 | +0.2 KB |
| ZI-data | 7540 | 41624 | +34.1 KB |

RAM 的增量里 **32KB 是 `heap_4` 的静态堆数组**（`configTOTAL_HEAP_SIZE = 32 * 1024`，
任务栈、TCB、队列都从这个堆里分配），其余是 7 个任务的栈与调度器结构。
STM32F407VE 的 512KB Flash / 192KB RAM 余量足够，但**这确实是拿资源换架构**：
换来了"等待外部条件"可以写成阻塞调用、以及各外设真正解耦；
代价是多出了一整套新的失败模式，必须靠 7.6 的诊断基建兜住。

---

## 八、重构原则与保真度

重构分两步走，边界很清楚：

1. **纯拆分**（保真）：把 2265 行的单文件按职责拆成应用模块。
   这个阶段的原则是"只搬代码、不改逻辑"——函数体逐字节取自原 `main.c`，
   只把跨模块调用的函数去掉 `static` 并在头文件声明，
   把 file-scope 的共享状态集中到 `app_ctx.c`。
2. **RTOS 事件化**（第七节）：这一步**故意改变**了执行模型——
   轮询改成事件驱动、超时计数改成毫秒截止时间、
   `s_door_*` 等状态改为由任务独占读写。所有改动点都在第七节列出。

| | 原单文件 | 本仓库 |
|---|---|---|
| `main.c` | 2265 行 | 159 行（只剩开机初始化 + 启动调度器） |
| 应用模块 | 0（全部挤在一个文件里） | 11 个 `.c` + 12 个头文件，约 3300 行 |
| 内核 | — | `freertos/`（V202212.01，vendor 进仓库） |
| 编译 | — | 可独立编译（已实测 0 error / 0 warning）；WiFi 凭证先按「编译与烧录」步骤复制模板 |

**行为保真度**：RTOS 化之前的拆分与原 `main.c` 逻辑等价。
RTOS 化之后，业务判断（状态机分支、触点消抖阈值、超时数值、
云端指令解析、本地关键词表）逐条对照过原 `main.c` 的超级循环，未改动业务语义——
改的只是"谁在什么时候执行"。