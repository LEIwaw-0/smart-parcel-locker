# -*- coding: utf-8 -*-
"""
智能快递柜云平台（零成本版）
================================
架构：程序运行在演示电脑上，全部免费组件
  - HTTP 服务(8000) : H5 管理端 + 业务 API（投件/取件/查件/格口/通知）
  - TCP  服务(8090) : 与柜机 ESP8266 保持长连接，下发开锁/取件码指令，接收状态上报
  - SQLite(locker.db): 包裹台账、通知记录（单文件数据库，零成本）

业务流程
  - 投件：H5 输入手机号 -> 云端分配格口 + 生成6位取件码 -> 下发 OPEN(开锁)
          -> 工作人员放入包裹并关门 -> H5 点“完成投件” -> 下发 SET(取件码写入柜机Flash)
          -> 发送取件通知（模拟短信 / 微信推送 / 飞书）
  - 取件：用户在柜机键盘输6位码 -> 柜机本地校验并开锁（断网也能取）
          -> 柜机上报 PICKED -> 云端更新台账
  - 查件：H5 按 手机号/尾号/取件码/格口/状态 查询

运行：pip install -r requirements.txt && python server.py
     浏览器打开 http://127.0.0.1:8000
"""
import datetime
import json
import os
import random
import socket
import sqlite3
import threading
from pathlib import Path

import uvicorn
from fastapi import FastAPI
from fastapi.responses import FileResponse
from fastapi.staticfiles import StaticFiles
from pydantic import BaseModel

from notifier import Notifier
from ai_chat import AIChat

BASE_DIR = Path(__file__).resolve().parent
DB_PATH = BASE_DIR / "locker.db"
CONFIG_PATH = BASE_DIR / "config.json"
STATIC_DIR = BASE_DIR / "static"

TCP_HOST = "0.0.0.0"
TCP_PORT = int(os.environ.get("LOCKER_TCP_PORT", "8090"))
HTTP_PORT = int(os.environ.get("LOCKER_HTTP_PORT", "8000"))
NUM_BOXES = 4            # 格口数量（与固件 CODEBOX_COUNT 一致）
CODE_LEN = 6             # 取件码长度
CODE_ALIVE_STATUS = ("depositing", "deposited")   # 占用格口的状态
# 投件超时自动作废：H5 发起投件后长时间没点“完成投件”（柜机也没上报
# DEPOSITED），记录会一直卡在 depositing 占着格口。超过这个时间就删掉记录、
# 释放格口，避免出现“格口一直显示投件中”。
DEPOSIT_TIMEOUT_SEC = 600     # 10 分钟
DEPOSIT_CHECK_SEC = 30        # 后台检查周期
# 语音播报长度上限：SYN6288 单帧 SYN_MAX_TEXT=96 字节，
# 减去 "[v8][t5][m0]" 12 字节，GBK 每汉字 2 字节 -> 36 字留足余量
REPLY_MAX_CHARS = 36
RELAY_SYSTEM = (
    "你是智能快递柜的语音助手小英。你的回答会被直接语音播报，"
    "请用口语化中文，不超过30个字，不要用表情、markdown、括号或列表。"
)

notifier = Notifier(CONFIG_PATH)


def load_config():
    try:
        return json.loads(CONFIG_PATH.read_text(encoding="utf-8"))
    except Exception:
        return {}


_config = load_config()
ai_chat = AIChat(
    api_key=_config.get("dashscope_api_key", ""),
    model=_config.get("ai_model", "qwen-turbo"),
    system_prompt=_config.get("ai_system_prompt") or RELAY_SYSTEM,
    base_url=_config.get("ai_base_url") or None,
)

app = FastAPI(title="智能快递柜云平台")


# ================= 数据库 =================
def get_db():
    conn = sqlite3.connect(DB_PATH)
    conn.row_factory = sqlite3.Row
    return conn


def init_db():
    with get_db() as db:
        db.executescript(
            """
            CREATE TABLE IF NOT EXISTS packages (
                id          INTEGER PRIMARY KEY AUTOINCREMENT,
                box_id      INTEGER NOT NULL,
                phone       TEXT NOT NULL,
                phone_tail  TEXT NOT NULL,
                code        TEXT NOT NULL,
                status      TEXT NOT NULL DEFAULT 'depositing',
                created_at  TEXT NOT NULL,
                picked_at   TEXT
            );
            CREATE TABLE IF NOT EXISTS notifications (
                id       INTEGER PRIMARY KEY AUTOINCREMENT,
                phone    TEXT,
                box_id   INTEGER,
                code     TEXT,
                channel  TEXT,
                content  TEXT,
                ok       INTEGER,
                sent_at  TEXT
            );
            """
        )


def now_str():
    return datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")


# ================= 柜机 TCP 长连接 =================
locker_socket = None
locker_addr = None
locker_lock = threading.Lock()
locker_online = False
locker_status_mask = 0        # last box-occupancy bitmask reported by locker (bit i = box i+1)
locker_door_mask = 0          # last door-open bitmask reported by locker (bit i = box i+1, 1=open)
locker_last_seen = None       # ISO time of the last locker uplink (any command)
locker_box_codes = {}        # box_id(int) -> 6-digit code str, last reported by locker BOX lines
locker_events = []           # recent events from locker (dict: {time, type, text}); capped at 50
LOCKER_GRACE_SEC = 90        # 柜机抓天气时会短暂断开中继链路，用最近上报时间兜住


def locker_is_online():
    """柜机是否在线。柜机抓天气走“借道式 HTTP GET”，会主动断开中继 TCP
    十几秒再重连，这段时间用 locker_last_seen 的宽限期兜住，
    避免 H5 每 3 分钟闪一次“离线 / 门未知”。"""
    if locker_online:
        return True
    if not locker_last_seen:
        return False
    try:
        seen = datetime.datetime.strptime(locker_last_seen, "%Y-%m-%d %H:%M:%S")
    except ValueError:
        return False
    return (datetime.datetime.now() - seen).total_seconds() < LOCKER_GRACE_SEC


def send_to_locker(line):
    """向柜机发送一行指令。返回 True=已发送，False=柜机离线。"""
    global locker_socket
    with locker_lock:
        s = locker_socket
        if s is None:
            print("[CLOUD] send FAIL (offline): %s" % line, flush=True)
            return False
        try:
            s.sendall((line + "\n").encode("gbk", errors="ignore"))
            print("[CLOUD] send OK: %s" % line, flush=True)
            return True
        except Exception:
            locker_socket = None
            print("[CLOUD] send EXC: %s" % line, flush=True)
            return False


def handle_locker_line(line):
    """解析柜机上行指令：
       HELLO         柜机上线
       PICKED n      格口 n 已取件开锁（用户键盘取件成功）
       DEPOSITED n   格口 n 投件完成（柜机端确认）
    """
    global locker_status_mask, locker_door_mask, locker_last_seen, locker_box_codes, locker_events
    line = line.strip()
    if not line:
        return
    locker_last_seen = now_str()
    # AI 对话：柜机上行 CHAT:用户输入 -> 云端下行 AI:回复
    if line.upper().startswith("CHAT:"):
        text = line[5:].strip()
        reply = ai_chat.chat(text)
        if ai_chat.last_error is not None:
            print("[AI] 调用失败: %s" % ai_chat.last_error, flush=True)
        reply = reply.replace("\r", " ").replace("\n", " ").strip()[:REPLY_MAX_CHARS]
        if not reply:
            reply = "我没听清请再说一次"
        send_to_locker("AI:" + reply)
        print("[AI] %s -> %s" % (text, reply), flush=True)
        return
    parts = line.split()
    if not parts:
        return
    cmd = parts[0].upper()
    try:
        if cmd == "HELLO":
            print("[LOCKER] 柜机上线", flush=True)
        elif cmd == "PICKED" and len(parts) >= 2:
            box = int(parts[1])
            print("[LOCKER] 格口 %d 已取件" % box, flush=True)
            locker_box_codes.pop(box, None)
            finish_pickup(box)
        elif cmd == "DEPOSITED" and len(parts) >= 2:
            box = int(parts[1])
            print("[LOCKER] 格口 %d 投件完成" % box, flush=True)
            confirm_deposit(box)
        elif cmd == "STATUS" and len(parts) >= 2:
            # STATUS <occ_mask> <door_mask> [1:code|2:------|...]
            try:
                locker_status_mask = int(parts[1])
            except ValueError:
                pass
            if len(parts) >= 3:
                try:
                    locker_door_mask = int(parts[2])
                except ValueError:
                    pass
            # New: optional 3rd arg is compact box codes snapshot
            if len(parts) >= 4:
                locker_box_codes.clear()
                snap = parts[3]
                for item in snap.split("|"):
                    if ":" not in item: continue
                    num, code = item.split(":", 1)
                    try:
                        box = int(num)
                    except ValueError: continue
                    if box < 1 or box > 8: continue
                    if code and code != "------":
                        locker_box_codes[box] = code
            print("[LOCKER] STATUS occ=%s door=%s codes=%s" %
                  (locker_status_mask, locker_door_mask, locker_box_codes), flush=True)
            # Auto-sync DB with locker's real occupancy mask (fallback for missing
            # PICKED uplinks). Only fix DB when locker says free but DB still has
            # "deposited" (item should have been picked). Do NOT touch "depositing"
            # — that's a mid-deposit state where CodeBox is legitimately empty
            # because SET hasn't been sent yet.
            try:
                _sdb = sqlite3.connect(DB_PATH)
                _sdb.row_factory = sqlite3.Row
                for _b in range(1, NUM_BOXES + 1):
                    _occupied = bool(locker_status_mask & (1 << (_b - 1)))
                    _row = _sdb.execute(
                        "SELECT status FROM packages WHERE box_id=? ORDER BY id DESC LIMIT 1",
                        (_b,)
                    ).fetchone()
                    _db_status = _row["status"] if _row else None
                    # Only fix: DB says "deposited" (item still there) but locker
                    # says free (item was taken). Do NOT auto-cancel "depositing".
                    if _db_status == "deposited" and not _occupied:
                        _sdb.execute(
                            "UPDATE packages SET status='picked', picked_at=? "
                            "WHERE box_id=? AND status='deposited'",
                            (now_str(), _b)
                        )
                        _sdb.commit()
                        print("[SYNC] DB box %d deposited->picked (locker says free)" % _b, flush=True)
                _sdb.close()
            except Exception as _e:
                print("[SYNC] DB sync error: %s" % _e, flush=True)
        elif cmd == "BOX" and len(parts) >= 4:
            # BOX <N> <0|1> <code6|------>  柜机主动上报单格口（兼容旧协议）
            box = int(parts[1])
            occ = int(parts[2])
            code = parts[3]
            if box < 1 or box > 8:
                print("[LOCKER] BOX 非法柜号: %d" % box, flush=True)
            elif occ:
                locker_box_codes[box] = code
            else:
                locker_box_codes.pop(box, None)
            print("[LOCKER] BOX %d occ=%d code=%s" % (box, occ, code), flush=True)
        elif cmd == "BOXES" and len(parts) >= 2:
            # BOXES <1:code1|2:code2|...>  柜机一次性上报所有格口紧凑快照
            # 例如 "BOXES 1:------|2:567890|3:------|4:------"
            snapshot = parts[1]
            # 先清空旧 codes（快照是全量）
            locker_box_codes.clear()
            for item in snapshot.split("|"):
                if ":" not in item: continue
                num, code = item.split(":", 1)
                try:
                    box = int(num)
                except ValueError: continue
                if box < 1 or box > 8: continue
                if code and code != "------":
                    locker_box_codes[box] = code
            print("[LOCKER] BOXES snapshot: %s" % locker_box_codes, flush=True)
        elif cmd == "EVENT" and len(parts) >= 2:
            etype = parts[1].lower()
            # EVENT <type> [text...] - 仅 type 时 text 为空，避免 H5 显示 [type] type 冗余
            text = " ".join(parts[2:]) if len(parts) > 2 else ""
            ev = {"time": now_str(), "type": etype, "text": text}
            locker_events.insert(0, ev)
            if len(locker_events) > 50:
                del locker_events[50:]
            print("[LOCKER] EVENT %s %s" % (etype, text), flush=True)
        else:
            print("[LOCKER] 未识别指令: %r" % line, flush=True)
    except ValueError:
        print("[LOCKER] 指令格式错误: %r" % line, flush=True)


def tcp_server_loop():
    """TCP 服务：接受柜机连接（单柜机），读取并处理上行指令。"""
    global locker_socket, locker_addr, locker_online
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind((TCP_HOST, TCP_PORT))
    srv.listen(2)
    print("TCP 柜机服务监听: %s:%d" % (TCP_HOST, TCP_PORT), flush=True)
    while True:
        try:
            conn, addr = srv.accept()
        except Exception as e:
            print("[TCP] accept 错误: %s" % e, flush=True)
            time_sleep(1)
            continue
        with locker_lock:
            old = locker_socket
            if old is not None:
                try:
                    old.close()
                except Exception:
                    pass
            locker_socket = conn
            locker_addr = addr
            locker_online = True
        print("[LOCKER] 已连接: %s" % (addr,), flush=True)
        buf = b""
        try:
            conn.settimeout(120)
            while True:
                data = conn.recv(512)
                if not data:
                    break
                buf += data
                while b"\n" in buf:
                    line, buf = buf.split(b"\n", 1)
                    # 柜机上行是 GBK（CHAT: 后面带中文），ASCII 解码会把汉字变成 '?'
                    try:
                        text = line.decode("gbk")
                    except Exception:
                        text = line.decode("ascii", errors="replace")
                    handle_locker_line(text)
        except socket.timeout:
            pass
        except Exception as e:
            print("[LOCKER] 连接异常: %s" % e, flush=True)
        finally:
            with locker_lock:
                if locker_socket is conn:
                    locker_socket = None
                    locker_addr = None
                    locker_online = False
            try:
                conn.close()
            except Exception:
                pass
            print("[LOCKER] 连接断开: %s" % (addr,), flush=True)


def time_sleep(sec):
    threading.Event().wait(sec)


# ================= 业务逻辑 =================
def gen_code(exclude):
    while True:
        code = "".join(random.choice("0123456789") for _ in range(CODE_LEN))
        if code not in exclude:
            return code


def active_codes():
    with get_db() as db:
        rows = db.execute(
            "SELECT code FROM packages WHERE status IN (?, ?)", CODE_ALIVE_STATUS
        ).fetchall()
        return {r["code"] for r in rows}


def box_occupied(box):
    # 优先用柜机实时 mask（如果在线），回退到 DB
    if locker_is_online() and (locker_status_mask & (1 << (box - 1))):
        return True
    with get_db() as db:
        r = db.execute(
            "SELECT id FROM packages WHERE box_id=? AND status IN (?, ?) LIMIT 1",
            (box,) + CODE_ALIVE_STATUS,
        ).fetchone()
        return r is not None


def start_deposit(phone):
    """投件第一步：分配格口 + 生成取件码 + 下发开锁。"""
    phone = phone.strip()
    if not phone.isdigit() or not (6 <= len(phone) <= 11):
        return None, "手机号需为 6~11 位数字（至少输入尾号6位）"
    tail = phone[-6:]
    free_box = None
    for b in range(1, NUM_BOXES + 1):
        if not box_occupied(b):
            free_box = b
            break
    if free_box is None:
        return None, "所有格口已满"
    code = gen_code(active_codes())
    with get_db() as db:
        db.execute(
            "INSERT INTO packages (box_id, phone, phone_tail, code, status, created_at)"
            " VALUES (?, ?, ?, ?, 'depositing', ?)",
            (free_box, phone, tail, code, now_str()),
        )
    locker_ok = send_to_locker("OPEN %d" % free_box)
    return {"box_id": free_box, "code": code, "locker_ok": locker_ok}, None


def confirm_deposit(box):
    """投件第二步：柜机写入取件码 + 发送取件通知。"""
    with get_db() as db:
        row = db.execute(
            "SELECT * FROM packages WHERE box_id=? AND status='depositing'"
            " ORDER BY id DESC LIMIT 1",
            (box,),
        ).fetchone()
        if row is None:
            return None, "该格口没有待确认的投件"
        db.execute("UPDATE packages SET status='deposited' WHERE id=?", (row["id"],))
        phone, code = row["phone"], row["code"]
    send_to_locker("SET %d %s" % (box, code))
    content = "【智能快递柜】您的包裹已存入格口%d，取件码：%s，请凭码在24小时内取件。" % (box, code)
    ok, detail = notifier.send(phone, box, code, content)
    with get_db() as db:
        db.execute(
            "INSERT INTO notifications (phone, box_id, code, channel, content, ok, sent_at)"
            " VALUES (?, ?, ?, ?, ?, ?, ?)",
            (phone, box, code, detail.get("channel", "log"),
             content, 1 if ok else 0, now_str()),
        )
    return {"box_id": box, "code": code, "notify_ok": ok, "notify_detail": detail}, None


def cancel_deposit(box):
    """取消投件：删除未完成的投件记录，释放格口。"""
    with get_db() as db:
        cur = db.execute(
            "DELETE FROM packages WHERE box_id=? AND status='depositing'", (box,)
        )
        return cur.rowcount > 0


def expire_stale_deposits():
    """投件超时自动作废：删除 created_at 早于阈值的 depositing 记录以释放格口。"""
    cutoff = (
        datetime.datetime.now() - datetime.timedelta(seconds=DEPOSIT_TIMEOUT_SEC)
    ).strftime("%Y-%m-%d %H:%M:%S")
    with get_db() as db:
        rows = db.execute(
            "SELECT box_id, code FROM packages"
            " WHERE status='depositing' AND created_at < ?",
            (cutoff,),
        ).fetchall()
        if not rows:
            return
        db.execute(
            "DELETE FROM packages WHERE status='depositing' AND created_at < ?",
            (cutoff,),
        )
    for r in rows:
        print(
            "[EXPIRE] 格口 %d 投件超时(>%ds)已作废，取件码 %s，格口已释放"
            % (r["box_id"], DEPOSIT_TIMEOUT_SEC, r["code"]),
            flush=True,
        )


def expire_loop():
    while True:
        time_sleep(DEPOSIT_CHECK_SEC)
        try:
            expire_stale_deposits()
        except Exception as e:
            print("[EXPIRE] 检查失败: %s" % e, flush=True)


def finish_pickup(box):
    """用户取件完成：将已投状态改为已取，清空格口。"""
    with get_db() as db:
        row = db.execute(
            "SELECT * FROM packages WHERE box_id=? AND status='deposited'"
            " ORDER BY id DESC LIMIT 1",
            (box,),
        ).fetchone()
        if row is None:
            return
        db.execute(
            "UPDATE packages SET status='picked', picked_at=? WHERE id=?",
            (now_str(), row["id"]),
        )


# ================= HTTP API =================
class DepositStartBody(BaseModel):
    phone: str


class BoxOpBody(BaseModel):
    box_id: int


@app.get("/api/status")
def api_status():
    """格口实时状态 + 柜机在线状态（H5 每 3 秒轮询）。"""
    with get_db() as db:
        rows = db.execute(
            "SELECT * FROM packages WHERE status IN (?, ?) ORDER BY box_id",
            CODE_ALIVE_STATUS,
        ).fetchall()
    occ = {r["box_id"]: r for r in rows}
    online = locker_is_online()
    boxes = []
    for b in range(1, NUM_BOXES + 1):
        r = occ.get(b)
        db_status = r["status"] if r else "free"
        db_code = r["code"] if r else None
        db_phone = r["phone_tail"] if r else None
        # 柜机实时占用（mask bit i = box i+1）
        locker_occupied = bool(online and (locker_status_mask & (1 << (b - 1))))
        # 柜机实时上报的取件码（BOX 行）
        live_code = locker_box_codes.get(b) or locker_box_codes.get(str(b))
        # 综合状态：柜机在线时以柜机占用为准
        if locker_occupied:
            if db_status == "free":
                final_status = "occupied_local"   # 柜机本地存件，DB 无记录
                source = "locker"
            else:
                final_status = db_status           # 云端投件，柜机确认
                source = "cloud"
        else:
            final_status = db_status
            source = "db"
        boxes.append(
            {
                "box_id": b,
                "status": final_status,
                "phone_tail": db_phone,
                "code": db_code or live_code,
                "code_live": live_code,
                "occupied": locker_occupied,
                "source": source,
                "created_at": r["created_at"] if r else None,
            }
        )
    return {"locker_online": online, "boxes": boxes,
            "locker_status_mask": locker_status_mask,
            "locker_door_mask": locker_door_mask,
            "locker_last_seen": locker_last_seen,
            "locker_box_codes": locker_box_codes,
            "locker_events": locker_events[:10],
            "time": now_str()}


@app.post("/api/deposit/start")
def api_deposit_start(body: DepositStartBody):
    data, err = start_deposit(body.phone)
    if err:
        return {"ok": False, "error": err}
    return {"ok": True, **data}


@app.post("/api/deposit/confirm")
def api_deposit_confirm(body: BoxOpBody):
    data, err = confirm_deposit(body.box_id)
    if err:
        return {"ok": False, "error": err}
    return {"ok": True, **data}


@app.post("/api/deposit/cancel")
def api_deposit_cancel(body: BoxOpBody):
    return {"ok": cancel_deposit(body.box_id)}


@app.post("/api/sim/pickup")
def api_sim_pickup(body: BoxOpBody):
    """模拟取件：无柜机硬件时用于演示，效果等同柜机上报 PICKED。"""
    finish_pickup(body.box_id)
    return {"ok": True}


@app.post("/api/admin/open")
def api_admin_open(body: BoxOpBody):
    """管理员远程开锁：直接下发 UNLOCK 指令打开指定柜门。
    不走投件/取件台账，不影响已写入的取件码，仅开锁。"""
    if body.box_id < 1 or body.box_id > NUM_BOXES:
        return {"ok": False, "error": "格口编号超出范围"}
    sent = send_to_locker("UNLOCK %d" % body.box_id)
    return {"ok": bool(sent), "locker_online": locker_is_online()}


@app.get("/api/packages")
def api_packages(q: str = "", status: str = ""):
    """查件：按 手机号/尾号/取件码/格口 模糊查询，可叠加状态筛选。"""
    sql = "SELECT * FROM packages WHERE 1=1"
    args = []
    if q.strip():
        like = "%" + q.strip() + "%"
        sql += (" AND (phone LIKE ? OR phone_tail LIKE ? OR code LIKE ?"
                " OR CAST(box_id AS TEXT) LIKE ?)")
        args += [like, like, like, like]
    if status.strip():
        sql += " AND status=?"
        args.append(status.strip())
    sql += " ORDER BY id DESC LIMIT 100"
    with get_db() as db:
        rows = db.execute(sql, args).fetchall()
    return {"rows": [dict(r) for r in rows]}


@app.get("/api/notifications")
def api_notifications():
    with get_db() as db:
        rows = db.execute(
            "SELECT * FROM notifications ORDER BY id DESC LIMIT 50"
        ).fetchall()
    return {"rows": [dict(r) for r in rows]}


@app.get("/")
def index():
    return FileResponse(STATIC_DIR / "index.html")


app.mount("/static", StaticFiles(directory=str(STATIC_DIR)), name="static")


def main():
    init_db()
    threading.Thread(target=tcp_server_loop, daemon=True).start()
    threading.Thread(target=expire_loop, daemon=True).start()
    print("=" * 56, flush=True)
    print("  智能快递柜云平台已启动（零成本版）", flush=True)
    print("  H5 管理端 : http://127.0.0.1:%d" % HTTP_PORT, flush=True)
    print("  柜机 TCP  : %s:%d" % (TCP_HOST, TCP_PORT), flush=True)
    print("=" * 56, flush=True)
    uvicorn.run(app, host="0.0.0.0", port=HTTP_PORT, log_level="warning")


if __name__ == "__main__":
    main()
