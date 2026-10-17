"""Private host connection, state reporting and redaction for the badge conversation."""

import hashlib
import json
import os
from pathlib import Path
import re
import select
import shutil
import socket
import subprocess
import threading
import time


def redact(text):
    text = re.sub(r'\b(Bearer|Basic)\s+[^\s"\']+', r'\1 ***', text, flags=re.I)

    def replace(match):
        key, separator, value = match.groups()
        return match[0] if value.lower() in ("bearer", "basic") else key + separator + "***"

    return re.sub(
        r'\b([\w.-]*(?:token|secret|password|passwd|api[_-]?key|auth)[\w.-]*)(\s*[=:]\s*|\s+)("(?:\\.|[^"\\])*"|\'[^\']*\'|\S+)',
        replace,
        text,
        flags=re.I,
    )


def detail(tool, args):
    tool = tool if isinstance(tool, str) else "tool"
    if os.environ.get("POCKET_AGENT_DETAIL") == "0":
        return redact(tool)[:40]
    args = args if isinstance(args, dict) else {}
    command = args.get("command")
    filename = args.get("path", args.get("file_path"))
    if tool in ("terminal", "bash", "execute_code") and isinstance(command, str):
        text = "$ " + command[:4096].split("\n")[0]
    elif tool in ("read_file", "read", "write_file", "write", "edit_file", "edit") and isinstance(
        filename, str
    ):
        text = tool + " " + Path(filename).name
    else:
        text = tool
    return redact(text)[:40]


class Bridge:
    """One persistent owner socket; coalesce offline reports and replay on reconnect."""

    def __init__(self, home=None, socket_path=None, hub_script=None, voice_callback=None):
        self.home = Path(
            os.environ.get("POCKET_AGENT_HOME") or home or str(Path.home() / ".pocket-agent")
        )
        self.path = (
            os.environ.get("POCKET_AGENT_SOCKET") or socket_path or str(self.home / "hub.sock")
        )
        self.hub_script = (
            os.environ.get("POCKET_AGENT_HUB_SCRIPT")
            or hub_script
            or str(Path(__file__).resolve().parent.parent / "bridge" / "hub.mjs")
        )
        self.condition = threading.Condition()
        self.session = None
        self.sid = None
        self.stopped = False
        self.worker = None
        self.last_spawn = 0
        self.voice_callback = voice_callback
        self.outbox = []
        self.reply_callback = None
        self.reply_outbox = []
        self.reply_busy = False

    def reply_send(self, message):
        with self.condition:
            if message.get('t') == 'reply_cancel':
                self.reply_outbox = [
                    m for m in self.reply_outbox if m.get('reply') != message.get('reply')
                ]
                self.reply_outbox.insert(0, message)
                self.condition.notify_all()
                return
            deadline = time.monotonic() + 5
            while len(self.reply_outbox) >= 8 and not self.stopped and time.monotonic() < deadline:
                self.condition.wait(0.05)
            if self.stopped or len(self.reply_outbox) >= 8:
                raise ConnectionError('reply queue')
            self.reply_outbox.append(message)
            self.reply_busy = True
            self.condition.notify_all()

    def update(self, sid, state="idle", activity="", name="Hermes", voice=False):
        if not isinstance(sid, str) or not sid:
            return
        with self.condition:
            if self.sid is not None and sid != self.sid:
                return
            self.sid = sid
            wire_id = "hermes" + hashlib.sha256(sid.encode()).hexdigest()[:8]
            self.session = {
                "id": wire_id,
                "name": redact(name)[:24],
                "state": state,
                "detail": redact(activity)[:40],
                "voice": voice,
            }
            if self.worker is None:
                self.worker = threading.Thread(target=self._run, name="badge-bridge", daemon=True)
                self.worker.start()
            self.condition.notify()

    def voice_result(self, ident, turn, result):
        with self.condition:
            self.outbox = (
                self.outbox + [{"t": "voice_result", "id": ident, "turn": turn, "result": result}]
            )[-8:]
            self.condition.notify()

    def end(self, sid):
        with self.condition:
            if sid == self.sid:
                self.session = self.sid = None
            self.condition.notify()

    def close(self):
        with self.condition:
            self.stopped = True
            self.condition.notify()
        if self.worker and self.worker is not threading.current_thread():
            self.worker.join(timeout=1)

    def _start_hub(self):
        if time.monotonic() - self.last_spawn < 5:
            return
        self.last_spawn = time.monotonic()
        script = self.hub_script
        node = shutil.which("node")
        if node and Path(script).is_file():
            try:
                env = {
                    **os.environ,
                    "POCKET_AGENT_HOME": str(self.home),
                    "POCKET_AGENT_SOCKET": self.path,
                }
                subprocess.Popen(
                    [node, script],
                    env=env,
                    stdin=subprocess.DEVNULL,
                    stdout=subprocess.DEVNULL,
                    stderr=subprocess.DEVNULL,
                    start_new_session=True,
                )
            except OSError:
                pass

    def _run(self):
        peer = None
        sent = None
        rx = b''
        try:
            while True:
                with self.condition:
                    if self.stopped:
                        break
                    snapshot = dict(self.session) if self.session else None
                if not snapshot:
                    if peer:
                        peer.close()
                    peer = None
                    sent = None
                    with self.condition:
                        self.condition.wait(0.1)
                    continue
                try:
                    if peer is None:
                        peer = socket.socket(socket.AF_UNIX)
                        peer.settimeout(0.25)
                        peer.connect(self.path)
                        sent = None
                        rx = b''
                    messages = []
                    if sent != snapshot:
                        if sent and sent['id'] != snapshot['id']:
                            messages.append({'t': 'bye', 'id': sent['id']})
                        messages.extend(
                            (
                                {
                                    't': 'hello',
                                    'id': snapshot['id'],
                                    'name': snapshot['name'],
                                    'voice': snapshot['voice'],
                                },
                                {'t': 'state', **snapshot},
                            )
                        )
                    with self.condition:
                        messages.extend(self.outbox)
                        self.outbox = []
                        messages.extend(self.reply_outbox)
                        self.reply_outbox = []
                        self.condition.notify_all()
                    if messages:
                        peer.sendall("".join(json.dumps(m) + "\n" for m in messages).encode())
                    sent = snapshot
                    while select.select([peer], [], [], 0)[0]:
                        data = peer.recv(8192)
                        if not data:
                            raise ConnectionError("hub closed")
                        rx += data
                        if len(rx) > 65536:
                            raise ValueError('hub frame')
                        while b'\n' in rx:
                            line, rx = rx.split(b'\n', 1)
                            try:
                                msg = json.loads(line)
                                if (
                                    self.voice_callback
                                    and isinstance(msg, dict)
                                    and str(msg.get('t', '')).startswith('voice_')
                                ):
                                    self.voice_callback(msg)
                                if (
                                    self.reply_callback
                                    and isinstance(msg, dict)
                                    and str(msg.get('t', '')).startswith('reply_')
                                ):
                                    self.reply_callback(msg)
                                    if msg.get('t') in ('reply_cancel', 'reply_result'):
                                        self.reply_busy = False
                            except Exception:
                                pass
                except (OSError, ValueError):
                    if peer:
                        peer.close()
                    peer = None
                    rx = b''
                    if self.voice_callback:
                        self.voice_callback({'t': 'voice_cancel'})
                    if self.reply_callback:
                        self.reply_callback({'t': 'disconnect'})
                    with self.condition:
                        self.reply_outbox = []
                        self.reply_busy = False
                        self.condition.notify_all()
                    self._start_hub()
                    with self.condition:
                        self.condition.wait(0.5)
                with self.condition:
                    self.condition.wait(0.01 if self.reply_busy else 0.1)
        finally:
            if peer:
                peer.close()
