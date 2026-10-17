"""Optional installed Hermes stdio acceptance; private profile and localhost provider only.

Use the already installed Hermes Python. Three synthetic microphone takes enter
the real VoiceInput and session protocol. The third continues after backend restart.
The bridge is a credit-aware isolated sink; no real badge or Discord messages.
"""

import base64
import hashlib
import http.server
import json
import math
import os
from pathlib import Path
import sqlite3
import struct
import sys
import tempfile
import threading
import time
import yaml

candidate = Path(sys.argv[1]).resolve()
base = Path(os.environ.get("POCKET_AGENT_BUILD", str(candidate.parent.parent / ".build"))).resolve()
base.mkdir(parents=True, exist_ok=True)
(base / "evidence").mkdir(exist_ok=True)
hermes = Path(os.environ.get('HERMES_REPO', str(Path.home() / '.hermes/hermes-agent'))).resolve()
sys.path.insert(0, str(Path(__file__).resolve().parent))
from support import load

mod = load('badge_chat', candidate / 'badge_chat.py')
requests = []
commands = [
    'Remember the badge fixture word mango.',
    'What fixture word did I say?',
    'After restart, recall the fixture word.',
]


class Model(http.server.BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_GET(self):
        data = b'{"data": [{"id": "badge-fixture"}]}'
        self.send_response(200)
        self.send_header('Content-Length', str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_POST(self):
        req = json.loads(self.rfile.read(int(self.headers['Content-Length'])))
        requests.append(req)
        (base / "evidence/badge-chat-fixture-users.json").write_text(
            json.dumps(
                [
                    [m.get("content") for m in r.get("messages", []) if m.get("role") == "user"]
                    for r in requests
                ],
                indent=2,
            )
        )
        answer = 'The badge fixture word is mango.'
        if req.get('stream'):
            self.send_response(200)
            self.send_header('Content-Type', 'text/event-stream')
            self.end_headers()
            for delta, finish in [
                ({'role': 'assistant', 'content': ''}, None),
                ({'content': answer}, None),
                ({}, 'stop'),
            ]:
                frame = {
                    'id': 'badge-fixture',
                    'object': 'chat.completion.chunk',
                    'created': 1,
                    'model': 'badge-fixture',
                    'choices': [{'index': 0, 'delta': delta, 'finish_reason': finish}],
                }
                self.wfile.write(('data: ' + json.dumps(frame) + '\n\n').encode())
                self.wfile.flush()
            self.wfile.write(b'data: [DONE]\n\n')
        else:
            data = json.dumps(
                {
                    'id': 'badge-fixture',
                    'object': 'chat.completion',
                    'created': 1,
                    'model': 'badge-fixture',
                    'choices': [
                        {
                            'index': 0,
                            'message': {'role': 'assistant', 'content': answer},
                            'finish_reason': 'stop',
                        }
                    ],
                    'usage': {'prompt_tokens': 1, 'completion_tokens': 8, 'total_tokens': 9},
                }
            ).encode()
            self.send_response(200)
            self.send_header('Content-Type', 'application/json')
            self.send_header('Content-Length', str(len(data)))
            self.end_headers()
            self.wfile.write(data)


class Sink:
    def __init__(self):
        self.frames = []
        self.results = []
        self.states = []
        self.payload = bytearray()

    def update(self, *args, **kwargs):
        self.states.append((args, kwargs))

    def voice_result(self, *args):
        self.results.append(args)

    def reply_send(self, frame):
        self.frames.append(frame)
        if frame['t'] == 'reply_begin':
            self.payload.clear()
            assert set(frame) == {'t', 'id', 'turn', 'reply', 'text_bytes'}
            self.reply_callback({'t': 'reply_credit', 'reply': frame['reply'], 'count': 8})
        elif frame['t'] == 'reply_data':
            self.payload.extend(base64.b64decode(frame['data']))
        elif frame['t'] == 'reply_end':
            assert (
                len(self.payload) == frame['bytes']
                and hashlib.sha256(self.payload).hexdigest() == frame['digest']
            )
            self.reply_callback({'t': 'reply_result', 'reply': frame['reply'], 'result': 'text'})

    def end(self, *args):
        pass

    def close(self):
        pass


def wait(check, seconds=60):
    end = time.monotonic() + seconds
    while not check() and time.monotonic() < end:
        time.sleep(0.05)
    assert check(), 'Installed Hermes fixture timed out'


server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Model)
threading.Thread(target=server.serve_forever, daemon=True).start()
chat = None
report = {
    'source': 'installed Hermes stdio backend, actual VoiceInput and ReplyOutput; private profile, localhost provider and synthetic microphone takes',
    'external_model_requests': 0,
    'live_discord_messages': 0,
    'real_board_io': False,
}
try:
    with tempfile.TemporaryDirectory(prefix='badge-chat-runtime-', dir=base) as folder:
        home = Path(folder)
        (home / 'empty-bundled').mkdir()
        (home / 'model').mkdir()
        (home / 'model/model.bin').touch()
        cfg = {
            'model': {
                'default': 'badge-fixture',
                'provider': 'custom',
                'base_url': f'http://127.0.0.1:{server.server_port}/v1',
                'api_mode': 'chat_completions',
            },
            'security': {'allow_lazy_installs': False},
            'plugins': {'enabled': []},
            'mcp_servers': {},
            'wake_word': {'enabled': False},
            'agent': {'max_turns': 2},
            'display': {'show_reasoning': False},
            'toolsets': {'tui': ['terminal']},
        }
        (home / 'config.yaml').write_text(yaml.safe_dump(cfg))
        env = {
            **os.environ,
            'HERMES_HOME': str(home),
            'HERMES_BUNDLED_PLUGINS': str(home / 'empty-bundled'),
            'HERMES_ENABLE_PROJECT_PLUGINS': '0',
            'HERMES_DISABLE_LAZY_INSTALLS': '1',
            'HERMES_RUNTIME_DIR': str(Path.home() / '.hermes/tools'),
            'PYTHONPYCACHEPREFIX': str(base / 'pycache'),
            'OPENAI_API_KEY': 'fixture-dummy',
            'OPENAI_BASE_URL': f'http://127.0.0.1:{server.server_port}/v1',
        }
        guard = '''import socket
real_connect=socket.socket.connect
real_connect_ex=socket.socket.connect_ex
def permitted(sock,address):
 if sock.family in (socket.AF_INET,socket.AF_INET6) and address[0] not in ('127.0.0.1','::1','localhost'):raise PermissionError('Fixture forbids external connections')
def connect(sock,address):
 permitted(sock,address);return real_connect(sock,address)
def connect_ex(sock,address):
 permitted(sock,address);return real_connect_ex(sock,address)
socket.socket.connect=connect
socket.socket.connect_ex=connect_ex
'''
        code = (
            'import sys;sys.path.insert(0,'
            + repr(str(hermes))
            + ');exec('
            + repr(guard)
            + ');from tui_gateway.entry import main;main()'
        )
        reference = home / 'badge-chat.json'
        index = [0]

        def transcribe(path):
            return {'success': True, 'transcript': commands[index[0]]}

        def open_chat():
            sink = Sink()
            value = mod.BadgeChat(
                home,
                reference,
                sink,
                str(home / 'model'),
                lambda receive: mod.RPC(
                    [sys.executable, '-I', '-u', '-c', code], env, folder, receive
                ),
                transcribe=transcribe,
            )
            try:
                return value.start(), sink
            except Exception:
                value.close()
                raise

        chat, sink = open_chat()
        assert not requests and chat.allowed(chat.ident), (
            'ready before first prompt without provider call'
        )
        first_key, first_assistant = chat.stored, chat.ident

        def take(n):
            index[0] = n
            turn = f'{n + 1:016x}'
            prior = sum(f['t'] == 'reply_end' for f in sink.frames)
            pcm = b''.join(struct.pack('<h', int(1600 * math.sin(i * 0.2))) for i in range(6400))
            chat.voice_receive({'t': 'voice_begin', 'id': chat.ident, 'turn': turn})
            for seq, offset in enumerate(range(0, len(pcm), 384)):
                chat.voice_receive(
                    {
                        't': 'voice_data',
                        'id': chat.ident,
                        'turn': turn,
                        'seq': seq,
                        'data': base64.b64encode(pcm[offset : offset + 384]).decode(),
                    }
                )
            chat.voice_receive(
                {
                    't': 'voice_end',
                    'id': chat.ident,
                    'turn': turn,
                    'bytes': len(pcm),
                    'digest': hashlib.sha256(pcm).hexdigest(),
                }
            )
            wait(lambda: sum(f['t'] == 'reply_end' for f in sink.frames) > prior)
            wait(lambda: any(r[1] == turn and r[2] == 'submitted' for r in sink.results))
            assert b'mango' in sink.payload and not chat.busy

        take(0)
        take(1)
        assert any(
            any(
                isinstance(m.get('content'), str)
                and m['content'].split('\n', 1)[0] == 'Spoken command: ' + commands[0]
                for m in req.get('messages', [])
            )
            and any(
                isinstance(m.get('content'), str)
                and m['content'].split('\n', 1)[0] == 'Spoken command: ' + commands[1]
                for m in req.get('messages', [])
            )
            for req in requests
        ), 'follow-up omitted first command'
        chat.close()
        chat, sink = open_chat()
        assert (
            chat.stored == first_key and chat.ident == first_assistant and chat.allowed(chat.ident)
        ), 'resume must retain conversation and assistant'
        take(2)
        assert any(
            all(
                any(
                    isinstance(m.get('content'), str)
                    and m['content'].split('\n', 1)[0] == 'Spoken command: ' + command
                    for m in req.get('messages', [])
                )
                for command in commands
            )
            for req in requests
        ), 'restart lost conversation history'
        with sqlite3.connect(home / 'state.db') as db:
            rows = db.execute(
                'SELECT role,content FROM messages WHERE session_id=?', (first_key,)
            ).fetchall()
            assert (
                sum(
                    role == 'user' and content == 'Spoken command: ' + command
                    for role, content in rows
                    for command in commands
                )
                == 3
            )
        report.update(
            ok=True,
            synthetic_voice_commands=3,
            same_stored_conversation=True,
            stable_assistant_across_restart=True,
            restart_history_retained=True,
            text_only=True,
            provider_requests=len(requests),
        )
        print(json.dumps(report), flush=True)
finally:
    if chat:
        chat.close()
    server.shutdown()
    server.server_close()
    evidence = base / 'evidence/badge-chat-runtime.json'
    evidence.write_text(json.dumps(report, indent=2) + '\n')
