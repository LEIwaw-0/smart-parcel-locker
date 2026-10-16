# -*- coding: utf-8 -*-
"""
通义千问 AI 对话模块（DashScope OpenAI 兼容模式）
- 维护多轮对话上下文
- 无 API Key 时返回友好提示，不崩溃
- 回复控制在简短范围内，适合 TTS 播报
"""
import json
import urllib.request

DEFAULT_SYSTEM = (
    "你是智能快递柜的AI助手小英，回答简洁友好，"
    "每次回复不超过60字，适合语音播报。"
)
# 通用 DashScope 域名（sk- 开头的 key 用这个）
DEFAULT_BASE_URL = "https://dashscope.aliyuncs.com/compatible-mode/v1"


class AIChat:
    def __init__(self, api_key="", model="qwen-turbo",
                 system_prompt=None, max_history=10, base_url=None):
        self.api_key = (api_key or "").strip()
        self.model = model or "qwen-turbo"
        self.system = system_prompt or DEFAULT_SYSTEM
        self.history = []          # [{"role": "user"/"assistant", "content": ...}]
        self.max_history = max_history
        self.last_error = None     # None = 上次调用成功, 否则为失败原因
        # 业务空间专属 key (sk-ws- 开头) 需要用专属域名，在 config.json 配置
        self.api_url = (base_url or DEFAULT_BASE_URL).rstrip("/") + "/chat/completions"

    def reset(self):
        self.history = []

    def chat(self, user_text):
        self.last_error = None
        user_text = (user_text or "").strip()
        if not user_text:
            return ""
        if not self.api_key:
            self.last_error = 'no-key'
            return ("AI未配置：请在cloud/config.json填写dashscope_api_key"
                    "（通义千问DashScope免费获取）")
        self.history.append({"role": "user", "content": user_text})
        messages = [{"role": "system", "content": self.system}]
        messages += self.history[-self.max_history:]
        try:
            payload = json.dumps({
                "model": self.model,
                "messages": messages,
                "temperature": 0.7,
                "max_tokens": 200,
            }).encode("utf-8")
            req = urllib.request.Request(
                self.api_url, data=payload, method="POST",
                headers={
                    "Authorization": "Bearer " + self.api_key,
                    "Content-Type": "application/json",
                },
            )
            with urllib.request.urlopen(req, timeout=20) as resp:
                data = json.loads(resp.read().decode("utf-8"))
            reply = data["choices"][0]["message"]["content"].strip()
            # 去掉可能的换行，TTS 播报更顺
            reply = reply.replace("\r", " ").replace("\n", " ")
            self.history.append({"role": "assistant", "content": reply})
            return reply
        except Exception as e:
            # 失败时移除刚加入的 user 消息，保持上下文干净
            if self.history and self.history[-1]["role"] == "user":
                self.history.pop()
            self.last_error = str(e)
            return "AI暂时无法回答：" + str(e)[:40]
