# -*- coding: utf-8 -*-
"""
通知模块：投件后向收件人发送“取件码通知”。

免费通道（毕设/演示推荐）：
  - log      默认，模拟短信：不真正发送，记录在系统内，H5“通知”页可查看（答辩演示用）
  - serverchan  Server酱：微信推送，需先到 https://sct.ftqq.com 用 GitHub 登录获取 SendKey
  - pushplus     PushPlus：微信推送，需到 https://www.pushplus.plus 获取 token
  - feishu       飞书群机器人：在飞书群添加“自定义机器人”，复制 webhook 地址

修改 config.json 中的 channel 与对应密钥即可切换，无需改代码。
"""
import json
from pathlib import Path

import requests


class Notifier:
    def __init__(self, config_path):
        self.config_path = Path(config_path)
        self._cfg = {}

    def load(self):
        try:
            self._cfg = json.loads(self.config_path.read_text(encoding="utf-8"))
        except Exception:
            self._cfg = {}
        return self._cfg

    def send(self, phone, box_id, code, content):
        """发送通知。返回 (ok, detail)，detail 含 channel 字段，供记录。"""
        cfg = self.load()
        channel = (cfg.get("channel") or "log").lower()
        try:
            if channel == "serverchan":
                key = (cfg.get("serverchan_key") or "").strip()
                if not key:
                    return False, {"channel": channel, "error": "未配置 serverchan_key"}
                r = requests.get(
                    "https://sctapi.ftqq.com/%s.send" % key,
                    params={"title": "快递柜取件通知", "desp": content},
                    timeout=10,
                )
                ok = r.status_code == 200 and r.json().get("code") == 0
                return ok, {"channel": channel, "resp": r.text[:120]}

            if channel == "pushplus":
                token = (cfg.get("pushplus_token") or "").strip()
                if not token:
                    return False, {"channel": channel, "error": "未配置 pushplus_token"}
                r = requests.post(
                    "http://www.pushplus.plus/send",
                    json={"token": token, "title": "快递柜取件通知", "content": content},
                    timeout=10,
                )
                ok = r.status_code == 200 and r.json().get("code") == 200
                return ok, {"channel": channel, "resp": r.text[:120]}

            if channel == "feishu":
                webhook = (cfg.get("feishu_webhook") or "").strip()
                if not webhook:
                    return False, {"channel": channel, "error": "未配置 feishu_webhook"}
                r = requests.post(
                    webhook,
                    json={"msg_type": "text", "content": {"text": content}},
                    timeout=10,
                )
                ok = r.status_code == 200 and r.json().get("code") == 0
                return ok, {"channel": channel, "resp": r.text[:120]}
        except Exception as e:
            return False, {"channel": channel, "error": str(e)}

        # 默认 log：模拟短信，不实际发送
        return True, {"channel": "log", "note": "模拟短信（未实际发送，仅演示）"}
