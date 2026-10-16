# 智能快递柜云平台（零成本版）

面向毕设/演示的快递柜投件·取件·查件云平台。**全部组件免费**：后端跑在本机、SQLite 单文件数据库、通知用模拟短信或免费推送通道。

```
┌─────────────┐  局域网 / 内网穿透   ┌──────────────────────────┐
│ 工作人员手机 │ ──────────────────▶ │ 演示电脑（本机，0 成本）    │
│ (H5 管理端)  │  HTTP:8000         │  server.py                │
└─────────────┘                     │  ├─ HTTP 8000  H5+业务API │
                                    │  └─ TCP  8090  柜机长连接 │
┌─────────────┐  局域网 TCP:8090    │  SQLite locker.db（台账）  │
│ 快递柜       │ ◀──────────────────│  通知：模拟短信/微信/飞书   │
│ STM32+ESP8266│  长连接            └──────────────────────────┘
└─────────────┘
```

## 一、快速开始（先跑通演示）

1. 安装依赖（本机需有 Python 3.8+）：
   ```
   pip install -r requirements.txt
   ```
2. 启动：
   ```
   python server.py
   ```
   或双击 `start.bat`。
3. 浏览器打开 **http://127.0.0.1:8000**，即 H5 管理端：
   - **投件**：输入收件人手机号 → 开始投件 → 显示"格口 n 已开锁 + 取件码" → 放入包裹关门 → 点"完成投件" → 生成取件通知
   - **查件**：按 手机号 / 尾号 / 取件码 / 格口号 查询，可筛选状态
   - **格口**：4 个格口实时状态（每 3 秒刷新）
   - **通知**：查看"模拟短信"通知记录（log 模式默认）

> 没有柜机硬件也能完整演示：服务端检测到柜机离线时自动进入演示模式，投件/查件/取件全流程可用，H5 提供"模拟取件"按钮。

## 二、工作人员手机访问

演示电脑与手机连同一 WiFi：

1. 查演示电脑局域网 IP：`ipconfig` 找 IPv4 地址（如 `192.168.1.100`）
2. 手机浏览器打开 **http://192.168.1.100:8000**

> 若 Windows 防火墙拦截，放行 Python 的 8000（HTTP）和 8090（TCP）端口：
> `netsh advfirewall firewall add rule name="locker8000" dir=in action=allow protocol=TCP localport=8000`
> `netsh advfirewall firewall add rule name="locker8090" dir=in action=allow protocol=TCP localport=8090`

## 三、柜机固件对接

柜机 ESP8266 继续当 **TCP 客户端**，连接地址改成演示电脑：

- 修改 `../esp8266.h`：`ESP_TCP_IP` = 演示电脑局域网 IP，`ESP_TCP_PORT` 保持 **8090**
- 协议为一行一行 ASCII 文本（与现有聊天中继同一传输形态），指令如下：

| 方向 | 指令 | 含义 |
|---|---|---|
| 云端→柜机 | `OPEN n` | 打开格口 n（投件开箱） |
| 云端→柜机 | `SET n 123456` | 将 6 位取件码 123456 写入格口 n（Flash 持久化） |
| 云端→柜机 | `CLEAR n` | 清空格口 n |
| 柜机→云端 | `HELLO` | 上线握手 |
| 柜机→云端 | `PICKED n` | 格口 n 用户键盘取件成功（本地校验通过、已开锁） |
| 柜机→云端 | `DEPOSITED n` | 格口 n 投件完成（可选，云端以 H5 确认为主） |

### 固件改造清单（下一步进行，需 Keil 编译烧录）

1. `esp8266.h`：改连接 IP/端口
2. `codebox.c/h`：新增 `CodeBox_SetEntry(box, phone, code)` —— 把云端下发的取件码写入 Flash（可复用现有整表擦写逻辑）
3. `main.c`：收到 `OPEN/SET/CLEAR` 时执行开锁/写码/清格口；键盘取件成功后发 `PICKED n`

## 四、取件通知（免费通道）

修改 `config.json` 的 `channel` 即可切换，无需改代码：

| channel | 通道 | 说明 |
|---|---|---|
| `log`（默认） | 模拟短信 | 不真实发送，记录在"H5→通知"页，答辩演示最稳 |
| `serverchan` | Server酱微信推送 | 到 https://sct.ftqq.com 用 GitHub 登录免费拿 SendKey，填 `serverchan_key` |
| `pushplus` | PushPlus 微信推送 | 到 https://www.pushplus.plus 免费取 token，填 `pushplus_token` |
| `feishu` | 飞书群机器人 | 飞书群加"自定义机器人"复制 webhook，填 `feishu_webhook` |

> 真发短信给任意手机号需要短信服务商（如阿里云短信，约 0.04 元/条），演示阶段不建议，答辩时可说明"预留短信接口"。

## 五、外网访问（可选，答辩现场不在同一 WiFi 时）

用免费内网穿透把本机服务暴露到公网：

- **cpolar**（免费隧道）：`cpolar tcp 8090`、`cpolar http 8000`，得到两个公网地址
- **花生壳**：免费版 1 条映射，分别映射 TCP 8090 和 HTTP 8000
- **ngrok**：`ngrok tcp 8090`、`ngrok http 8000`

柜机 ESP8266 的 `ESP_TCP_IP` 改为穿透的 TCP 域名（AT+CIPSTART 支持域名），工作人员手机访问穿透的 HTTP 地址。

## 六、HTTP API 速查（答辩可展示）

| 方法 | 路径 | 说明 |
|---|---|---|
| GET | `/api/status` | 格口状态 + 柜机在线 |
| POST | `/api/deposit/start` | `{phone}` → 分配格口、生成取件码、下发开锁 |
| POST | `/api/deposit/confirm` | `{box_id}` → 写码、发通知 |
| POST | `/api/deposit/cancel` | `{box_id}` → 取消投件 |
| POST | `/api/sim/pickup` | `{box_id}` → 模拟取件（无硬件演示） |
| GET | `/api/packages?q=&status=` | 查件 |
| GET | `/api/notifications` | 通知记录 |

## 七、目录结构

```
cloud/
├── server.py        # 主程序：HTTP API + 柜机 TCP 服务 + SQLite
├── notifier.py      # 通知通道（log/serverchan/pushplus/feishu）
├── config.json      # 通知通道配置
├── requirements.txt # 依赖
├── start.bat        # Windows 一键启动
└── static/index.html # H5 管理端
```
