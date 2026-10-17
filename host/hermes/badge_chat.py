"""One persistent private badge conversation over Hermes's stdio JSON-RPC API."""

import argparse
import fcntl
import json
import hashlib
import os
from pathlib import Path
import re
import signal
import sqlite3
import subprocess
import sys
import threading


import bridge as bridge_module
import voice_input as voice_module
import reply_output as reply_module

CHAT_NAME = 'Assistant'


class RPCError(Exception):
    def __init__(self, code):
        self.code = code
        super().__init__('Hermes RPC error')


class RPC:
    def __init__(self, command, env, cwd, receive):
        self.receive = receive
        self.lock = threading.RLock()
        self.write_lock = threading.Lock()
        self.pending = {}
        self.serial = 0
        self.dead = threading.Event()
        # Hermes owns its usual logs. Never copy content-bearing stderr into badge diagnostics.
        self.process = subprocess.Popen(
            command,
            env=env,
            cwd=cwd,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
        )
        self.reader = threading.Thread(target=self._read, name='badge-rpc', daemon=True)
        self.reader.start()

    def write(self, frame):
        data = (json.dumps(frame) + '\n').encode()
        with self.write_lock:
            if self.dead.is_set():
                raise ConnectionError('backend unavailable')
            self.process.stdin.write(data)
            self.process.stdin.flush()

    def call(self, method, params, timeout=30):
        with self.lock:
            self.serial += 1
            ident = 'badge-' + str(self.serial)
            slot = [threading.Event(), None]
            self.pending[ident] = slot
        try:
            self.write({'jsonrpc': '2.0', 'id': ident, 'method': method, 'params': params})
            if not slot[0].wait(timeout) or slot[1] is None:
                raise ConnectionError('backend response unavailable')
            if 'error' in slot[1]:
                raise RPCError(slot[1]['error'].get('code'))
            value = slot[1].get('result')
            if not isinstance(value, dict):
                raise ValueError('backend response shape')
            return value
        finally:
            with self.lock:
                self.pending.pop(ident, None)

    def answer(self, ident, result=None):
        self.write(
            {
                'jsonrpc': '2.0',
                'id': ident,
                **(
                    {'result': result}
                    if result is not None
                    else {'error': {'code': -32601, 'message': 'Unsupported on badge'}}
                ),
            }
        )

    def _read(self):
        try:
            while True:
                raw = self.process.stdout.readline(8 * 1024 * 1024 + 1)
                if not raw:
                    break
                if len(raw) > 8 * 1024 * 1024 or not raw.endswith(b'\n'):
                    raise ValueError('backend frame limit')
                try:
                    frame = json.loads(raw)
                except (ValueError, UnicodeError):
                    continue
                if not isinstance(frame, dict):
                    continue
                if 'method' in frame:
                    self.receive(frame)
                else:
                    with self.lock:
                        slot = self.pending.get(frame.get('id'))
                        if slot:
                            slot[1] = frame
                            slot[0].set()
        except Exception:
            pass
        finally:
            self.dead.set()
            with self.lock:
                for slot in self.pending.values():
                    slot[0].set()

    def close(self):
        try:
            self.process.stdin.close()
            self.process.wait(timeout=3)
        except (OSError, subprocess.TimeoutExpired):
            self.process.terminate()
            try:
                self.process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait(timeout=3)
        self.dead.set()


class BadgeChat:
    def __init__(self, home, reference, bridge, voice_model, rpc_factory, transcribe=None):
        self.home = Path(home).resolve()
        self.reference = Path(reference)
        self.bridge = bridge
        self.rpc_factory = rpc_factory
        self.lock = threading.RLock()
        self.rpc = None
        self.sid = self.stored = self.ident = None
        self.busy = False
        self.command = None
        self.requests = []
        self.closed = False
        self.ready = False
        self.used = False
        self.turn = None
        self.replies = reply_module.ReplyOutput(bridge.reply_send, self.valid, bridge_module.redact)
        self.voice = voice_module.VoiceInput(
            voice_model,
            reference.parent,
            self.allowed,
            self.submit,
            bridge.voice_result,
            transcribe=transcribe,
        )
        bridge.voice_callback = self.voice_receive
        bridge.reply_callback = self.reply_receive
        self.has_voice = (
            os.environ.get('POCKET_AGENT_VOICE') != '0'
            and (Path(voice_model) / 'model.bin').is_file()
        )

    def _load(self):
        if not self.reference.exists():
            return None
        if self.reference.stat().st_size > 4096:
            raise ValueError('reference size')
        value = json.loads(self.reference.read_text())
        if not isinstance(value, dict) or value.get('home') != str(self.home):
            raise ValueError('reference profile')
        key = value.get('stored')
        if (
            not isinstance(key, str)
            or not re.fullmatch(r'[A-Za-z0-9_-]{1,128}', key)
            or type(value.get('used')) is not bool
        ):
            raise ValueError('reference identity')
        self.used = value['used']
        return key

    def _save(self):
        self.reference.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
        temp = self.reference.with_suffix('.tmp')
        fd = os.open(temp, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
        with os.fdopen(fd, 'w') as out:
            json.dump({'home': str(self.home), 'stored': self.stored, 'used': self.used}, out)
            out.flush()
            os.fsync(out.fileno())
        temp.chmod(0o600)
        temp.replace(self.reference)

    def same_conversation(self, root, tip):
        if root == tip:
            return True
        db = None
        try:
            db = sqlite3.connect(
                (self.home / 'state.db').as_uri() + '?mode=ro', uri=True, timeout=0.1
            )
            db.execute('PRAGMA query_only=ON')
            seen = set()
            for _ in range(1024):
                if tip in seen:
                    return False
                seen.add(tip)
                row = db.execute(
                    'SELECT c.parent_session_id,c.model_config,c.source,p.end_reason FROM sessions c JOIN sessions p ON p.id=c.parent_session_id WHERE c.id=?',
                    (tip,),
                ).fetchone()
                if not row or row[2] != 'cli' or row[3] != 'compression':
                    return False
                config = json.loads(row[1]) if row[1] else {}
                if not isinstance(config, dict) or any(
                    config.get(k) is not None
                    for k in ('_branched_from', '_delegate_from', '_reset_from')
                ):
                    return False
                tip = row[0]
                if tip == root:
                    return True
            return False
        except (OSError, ValueError, sqlite3.Error):
            return False
        finally:
            if db:
                db.close()

    def start(self):
        saved = self._load()
        self.rpc = self.rpc_factory(self.receive)
        result = None
        if saved:
            try:
                # Lazy mount rejects missing IDs instead of materializing a replacement DB row.
                result = self.rpc.call(
                    'session.resume', {'session_id': saved, 'lazy': True, 'omit_messages': True}
                )
            except RPCError as exc:
                if exc.code != 4007 or self.used:
                    raise
        if result is None:
            result = self.rpc.call(
                'session.create',
                {
                    'title': CHAT_NAME,
                    'source': 'cli',
                    'idempotency_key': 'badge-chat',
                    'follow_profile_config': True,
                },
            )
        stored, sid = (
            result.get('stored_session_id') or result.get('resumed'),
            result.get('session_id'),
        )
        if (
            not isinstance(stored, str)
            or not re.fullmatch(r'[A-Za-z0-9_-]{1,128}', stored)
            or not isinstance(sid, str)
            or not sid
            or (saved and self.used and not self.same_conversation(saved, stored))
        ):
            raise ValueError('backend identity')
        with self.lock:
            self.stored, self.sid = saved if saved and self.used else stored, sid
            self.ident = hashlib.sha256(stored.encode()).hexdigest()[:8]
            self.ready = True
            self._save()
            self.report('idle', 'ready')
        return self

    def valid(self, sid):
        return (
            not self.closed
            and self.ready
            and sid == self.stored
            and self.rpc
            and not self.rpc.dead.is_set()
        )

    def allowed(self, ident):
        with self.lock:
            return bool(
                self.has_voice
                and ident == self.ident
                and self.valid(self.stored)
                and (not self.busy or self.requests)
            )

    def report(self, state, detail=''):
        if self.stored:
            self.bridge.update(
                self.stored, state, detail, CHAT_NAME, voice=self.allowed(self.ident)
            )

    def voice_receive(self, frame):
        if frame.get('t') in ('voice_begin', 'voice_cancel'):
            self.replies.cancel()
        self.voice.receive(frame)

    def reply_receive(self, frame):
        if frame.get('t') == 'disconnect':
            self.voice.cancel()
            self.replies.cancel()
        else:
            self.replies.receive(frame)

    def show(self, text, turn):
        if turn and self.command:
            self.replies.reply(self.stored, self.ident, turn, text)

    def _question_text(self):
        request = self.requests[0]
        params = request['params']
        if request['method'] == 'approval':
            return (
                'Hermes needs permission:\n'
                + str(params.get('description') or '')
                + '\n'
                + str(params.get('command') or '')
                + '\nHold blue and say "approve once" or "deny".'
            )
        question = params['questions'][request['index']]
        choices = question.get('choices') or []
        return (
            str(question.get('question') or 'Please answer Hermes.')
            + ('\n' + '\n'.join(str(c) for c in choices) if choices else '')
            + '\nHold blue to answer.'
        )

    def submit(self, ident, turn, text):
        with self.lock:
            if not self.allowed(ident):
                return False
            if self.requests:
                request = self.requests[0]
                words = text.removeprefix('Spoken command: ').strip()
                self.turn = turn
                if request['method'] == 'approval':
                    words = words.lower().strip(' .!?')
                    choice = {'approve once': 'once', 'deny': 'deny'}.get(words)
                    if choice is None or choice not in request['params'].get('choices', []):
                        self.show('Please say exactly "approve once" or "deny".', turn)
                        return False
                    result = {'choice': choice, 'all': False}
                else:
                    q = request['params']['questions'][request['index']]
                    request['answers'][q['qid']] = words
                    request['index'] += 1
                    if request['index'] < len(request['params']['questions']):
                        self.show(self._question_text(), turn)
                        return True
                    result = {'answers': request['answers']}
                self.requests.pop(0)
                self.rpc.answer(request['id'], result)
                if self.requests:
                    self.show(self._question_text(), turn)
                    self.report('waiting', 'needs you')
                else:
                    self.report('thinking')
                return True
            self.command, self.turn, self.busy = text, turn, True
            self.used = True
            self._save()
            self.report('thinking')
            sid = self.sid
        try:
            result = self.rpc.call('prompt.submit', {'session_id': sid, 'text': text})
            if result.get('status') != 'streaming':
                raise ValueError('unexpected acceptance')
            return True
        except Exception:
            # No replay after ambiguous acceptance. Backend teardown fences any delayed final.
            self.disconnected('command interrupted')
            self.rpc.close()
            return False

    def receive(self, frame):
        params = frame.get('params')
        if not isinstance(params, dict):
            return
        with self.lock:
            if self.closed or not self.ready or params.get('session_id') != self.sid:
                if 'id' in frame and 'method' in frame and frame['method'] != 'event':
                    self.rpc.answer(frame['id'])
                return
            kind = frame.get('method')
            if kind != 'event':
                if (
                    kind == 'approval'
                    and self.busy
                    and len(self.requests) < 5
                    and isinstance(params.get('choices'), list)
                    and 'deny' in params['choices']
                ):
                    self.requests.append({'id': frame['id'], 'method': kind, 'params': params})
                elif (
                    kind == 'clarify'
                    and self.busy
                    and len(self.requests) < 5
                    and isinstance(params.get('questions'), list)
                    and 0 < len(params['questions']) <= 5
                    and all(
                        isinstance(q, dict) and isinstance(q.get('qid'), str)
                        for q in params['questions']
                    )
                ):
                    self.requests.append(
                        {
                            'id': frame['id'],
                            'method': kind,
                            'params': params,
                            'index': 0,
                            'answers': {},
                        }
                    )
                else:
                    self.rpc.answer(frame['id'])
                    if self.busy:
                        self.show(
                            'This request needs a Mac interface and is unavailable on the badge.',
                            self.turn,
                        )
                    return
                if len(self.requests) == 1:
                    self.show(self._question_text(), self.turn)
                self.report('waiting', 'needs you')
                return
            event = params.get('type')
            payload = params.get('payload') or {}
            if not isinstance(payload, dict):
                return
            if event == 'request.cancel':
                self.requests = [r for r in self.requests if r['id'] != payload.get('id')]
                if self.requests:
                    self.show(self._question_text(), self.turn)
                self.report(
                    'waiting' if self.requests else 'thinking', 'needs you' if self.requests else ''
                )
            elif self.busy:
                if (
                    event in ('message.start', 'thinking.delta', 'reasoning.delta')
                    and not self.requests
                ):
                    self.report('thinking')
                elif event == 'tool.start' and not self.requests:
                    self.report(
                        'tool', bridge_module.detail(payload.get('name'), payload.get('args'))
                    )
                elif event == 'tool.complete' and not self.requests:
                    result = payload.get('result')
                    failed = isinstance(result, dict) and (
                        result.get('success') is False or result.get('error')
                    )
                    self.report('error' if failed else 'thinking', 'tool failed' if failed else '')
                elif event == 'error':
                    self.report('error', 'model request failed')
                elif event == 'message.complete':
                    self.requests.clear()
                    text = payload.get('text')
                    failed = payload.get('status') != 'complete'
                    if failed:
                        text = "I couldn't finish that command. Please try again."
                    self.show(text, self.turn)
                    self.busy = False
                    self.command = None
                    self.report(
                        'error' if failed else 'done',
                        'model request failed' if failed else 'your turn',
                    )

    def disconnected(self, detail='chat reconnecting'):
        with self.lock:
            self.ready = False
            self.busy = False
            self.command = None
            self.requests.clear()
            self.voice.cancel()
            self.replies.cancel()
            self.report('error', detail)

    def close(self):
        self.closed = True
        self.voice.close()
        self.replies.close()
        if self.rpc:
            self.rpc.close()
        if self.stored:
            self.bridge.end(self.stored)
        self.bridge.close()


def claim(path):
    path.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
    fd = os.open(path, os.O_RDWR | os.O_CREAT, 0o600)
    try:
        fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except Exception:
        os.close(fd)
        raise
    os.ftruncate(fd, 0)
    os.write(fd, str(os.getpid()).encode())
    return fd


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--hermes-home', type=Path, required=True)
    parser.add_argument('--hermes-repo', type=Path, required=True)
    parser.add_argument('--home', type=Path, required=True)
    parser.add_argument('--voice-model', type=Path, required=True)
    args = parser.parse_args()
    if os.environ.get('POCKET_AGENT') == '0':
        return
    os.environ.update(HERMES_HOME=str(args.hermes_home.resolve()), HERMES_DISABLE_LAZY_INSTALLS='1')
    sys.path.insert(0, str(args.hermes_repo.resolve()))
    fd = claim(args.home / 'badge-chat.lock')
    stopped = threading.Event()
    for sig in (signal.SIGTERM, signal.SIGINT):
        signal.signal(sig, lambda *_: stopped.set())
    env = {
        **os.environ,
        'HERMES_HOME': str(args.hermes_home.resolve()),
        'HERMES_DISABLE_LAZY_INSTALLS': '1',
        'PYTHONUNBUFFERED': '1',
    }
    code = (
        'import sys;sys.path.insert(0,'
        + repr(str(args.hermes_repo.resolve()))
        + ');from tui_gateway.entry import main;main()'
    )
    try:
        while not stopped.is_set():
            bridge = bridge_module.Bridge(home=str(args.home))
            chat = BadgeChat(
                args.hermes_home,
                args.home / 'badge-chat.json',
                bridge,
                str(args.voice_model),
                lambda receive: RPC(
                    [sys.executable, '-I', '-u', '-c', code], env, str(args.home), receive
                ),
            )
            try:
                chat.start()
                while not stopped.wait(0.2):
                    if chat.rpc.dead.is_set():
                        chat.disconnected()
                        break
            except Exception:
                # Content-free, stable recovery indication. Never print the exception body.
                bridge.update(
                    'badge-chat-unavailable', 'error', 'chat unavailable', CHAT_NAME, voice=False
                )
            finally:
                if not stopped.is_set():
                    stopped.wait(2)
                chat.close()
    finally:
        os.close(fd)


if __name__ == '__main__':
    main()
