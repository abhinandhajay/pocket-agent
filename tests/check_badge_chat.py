"""Isolated protocol tests for the dedicated badge-chat manager."""

import base64
import json
import os
import sqlite3
import subprocess
from pathlib import Path
import sys
import tempfile
import threading
import time

candidate = Path(sys.argv[1]).resolve()
from support import load

chatmod = load("chatmod", candidate / 'badge_chat.py')
from support import Checks, wait

check = Checks()


class Bridge:
    def __init__(self):
        self.updates = []
        self.frames = []
        self.payload = bytearray()
        self.ends = []

    def update(self, *args, **kwargs):
        self.updates.append((args, kwargs))

    def voice_result(self, *args):
        pass

    def reply_send(self, frame):
        self.frames.append(frame)
        if frame['t'] == 'reply_begin':
            self.payload.clear()
            self.reply_callback({'t': 'reply_credit', 'reply': frame['reply'], 'count': 8})
        elif frame['t'] == 'reply_data':
            self.payload.extend(base64.b64decode(frame['data']))

    def end(self, sid):
        self.ends.append(sid)

    def close(self):
        pass


class Peer:
    def __init__(self, receive, saved=None):
        self.receive = receive
        self.calls = []
        self.answers = []
        self.dead = threading.Event()
        self.saved = saved
        self.sid = 'runtime1'
        self.fail = None

    def call(self, method, params):
        self.calls.append((method, params))
        if method == 'session.resume':
            if self.saved is None:
                raise chatmod.RPCError(4007)
            return {'session_id': self.sid, 'resumed': self.saved}
        if method == 'session.create':
            return {'session_id': self.sid, 'stored_session_id': 'stored-badge-01'}
        if self.fail:
            raise self.fail
        return {'status': 'streaming'}

    def answer(self, ident, result=None):
        self.answers.append((ident, result))

    def close(self):
        self.dead.set()

    def event(self, kind, payload=None, sid=None):
        self.receive(
            {
                'method': 'event',
                'params': {'session_id': sid or self.sid, 'type': kind, 'payload': payload or {}},
            }
        )

    def request(self, ident, kind, **params):
        self.receive({'id': ident, 'method': kind, 'params': {'session_id': self.sid, **params}})


with tempfile.TemporaryDirectory() as folder:
    root = Path(folder)
    model = root / 'model'
    model.mkdir()
    (model / 'model.bin').touch()
    home = root / 'hermes'
    home.mkdir()
    ref = root / 'badge-chat.json'
    peers = []

    def factory(receive):
        peer = Peer(receive)
        peers.append(peer)
        return peer

    bridge = Bridge()
    chat = chatmod.BadgeChat(home, ref, bridge, str(model), factory).start()
    peer = peers[-1]
    check([m for m, _ in peer.calls] == ['session.create'], 'startup must not call model')
    check(
        bridge.updates[-1][0][3] == 'Assistant' and bridge.updates[-1][1]['voice'],
        'ready before first message',
    )
    check(ref.stat().st_mode & 0o777 == 0o600, 'private atomic reference')
    check(not chat.allowed('wrongassistant'), 'only owned assistant may submit')
    ident = chat.ident
    chat.show('unowned display text', '1234567890abcdef')
    check(not bridge.frames, 'reply without a badge command is ignored')
    peer.event('message.complete', {'text': 'unrelated', 'status': 'complete'})
    check(not bridge.frames, 'unsolicited final ignored')
    turn = '1234567890abcdef'
    check(chat.submit(ident, turn, 'Spoken command: First.'), 'first command accepted')
    check(not chat.allowed(ident), 'busy gates recording')
    check(not chat.submit(ident, turn, 'Spoken command: Duplicate.'), 'duplicate refused')
    check(json.loads(ref.read_text())['used'], 'submitted reference persists')
    peer.event(
        'tool.start', {'name': 'terminal', 'args': {'command': 'echo password=fixture-secret'}}
    )
    check(
        bridge.updates[-1][0][1] == 'tool' and 'fixture-secret' not in bridge.updates[-1][0][2],
        'tool activity redacted',
    )
    peer.event('tool.complete', {'result': {'success': False, 'error': 'secret raw result'}})
    check(bridge.updates[-1][0][1:3] == ('error', 'tool failed'), 'tool result withheld')
    peer.event('message.complete', {'text': 'Wrong runtime', 'status': 'complete'}, sid='other')
    check(not bridge.frames and chat.busy, 'other session fenced')
    peer.event('message.complete', {'text': '**Hello** token=fixture-secret', 'status': 'complete'})
    wait(lambda: any(f['t'] == 'reply_end' for f in bridge.frames))
    check(
        b'Hello' in bridge.payload and b'fixture-secret' not in bridge.payload,
        'final plain and redacted',
    )
    check(
        set(next(f for f in bridge.frames if f['t'] == 'reply_begin'))
        == {'t', 'id', 'turn', 'reply', 'text_bytes'},
        'text reply header',
    )
    check(chat.allowed(ident), 'ready for follow-up')
    ends = sum(f['t'] == 'reply_end' for f in bridge.frames)
    peer.event('message.complete', {'text': 'Duplicate final', 'status': 'complete'})
    time.sleep(0.02)
    check(sum(f['t'] == 'reply_end' for f in bridge.frames) == ends, 'one-shot final')
    check(chat.submit(ident, turn, 'Spoken command: Run a tool.'), 'second command')
    peer.request(
        'permit1',
        'approval',
        choices=['once', 'deny'],
        description='Run command',
        command='echo safe',
    )
    wait(lambda: b'approve once' in bridge.payload)
    check(chat.allowed(ident) and bridge.updates[-1][0][1] == 'waiting', 'approval waits for voice')
    check(
        not chat.submit(ident, '2234567890abcdef', 'Spoken command: Yes.'),
        'generic assent never grants permission',
    )
    check(not peer.answers, 'no approval sent on ambiguous words')
    check(
        chat.submit(ident, '3234567890abcdef', 'Spoken command: Approve once.'),
        'explicit approve once accepted',
    )
    check(
        peer.answers[-1] == ('permit1', {'choice': 'once', 'all': False}),
        'no lasting or blanket permission',
    )
    peer.request('permit2', 'approval', choices=['deny'], command='not permitted')
    check(not chat.submit(ident, turn, 'Spoken command: approve once'), 'unoffered choice rejected')
    check(chat.submit(ident, turn, 'Spoken command: deny'), 'explicit denial accepted')
    check(peer.answers[-1] == ('permit2', {'choice': 'deny', 'all': False}), 'denial stays native')
    peer.request(
        'clarify1',
        'clarify',
        questions=[
            {'qid': 'one', 'question': 'Which folder?', 'choices': None},
            {'qid': 'two', 'question': 'Which color?', 'choices': ['blue', 'red']},
        ],
    )
    check(chat.submit(ident, turn, 'Spoken command: This workspace.'), 'first answer')
    check(peer.answers[-1][0] == 'permit2' and chat.requests, 'batch not answered early')
    check(chat.submit(ident, turn, 'Spoken command: Blue.'), 'second answer')
    check(
        peer.answers[-1] == ('clarify1', {'answers': {'one': 'This workspace.', 'two': 'Blue.'}}),
        'question IDs preserved',
    )
    peer.request('secret1', 'secret', prompt='password=do-not-display')
    check(peer.answers[-1] == ('secret1', None), 'unsupported secret request declined')
    peer.event('message.complete', {'status': 'error', 'text': 'token=raw-model-error'})
    wait(lambda: b"couldn't finish" in bridge.payload)
    check(
        b'raw-model-error' not in bridge.payload and chat.allowed(ident),
        'error contained and retry available',
    )
    chat.close()
    check(bridge.ends == ['stored-badge-01'], 'owner removed on close')

    def resume(receive):
        peer = Peer(receive, 'stored-badge-01')
        peers.append(peer)
        return peer

    chat = chatmod.BadgeChat(home, ref, Bridge(), str(model), resume).start()
    check([m for m, _ in peers[-1].calls] == ['session.resume'], 'restart resumes exact chat')
    check(
        chat.ident == ident and peers[-1].calls[0][1]['lazy'],
        'stable assistant and missing-ID-safe resume',
    )
    peers[-1].fail = ConnectionError('timeout')
    check(
        not chat.submit(ident, turn, 'Spoken command: Uncertain.'),
        'ambiguous acceptance fails closed',
    )
    check(
        not chat.allowed(ident) and peers[-1].dead.is_set(),
        'delayed completion fenced and backend closed',
    )
    check(sum(m == 'prompt.submit' for m, _ in peers[-1].calls) == 1, 'never replay command')
    chat.close()
    for label, mutate in [('missing used', None), ('rebound', 'different-stored')]:

        def broken(receive, saved=mutate):
            peer = Peer(receive, saved)
            peers.append(peer)
            return peer

        chat = chatmod.BadgeChat(home, ref, Bridge(), str(model), broken)
        try:
            chat.start()
            raise AssertionError(label)
        except (chatmod.RPCError, ValueError):
            check(
                not any(m == 'session.create' for m, _ in peers[-1].calls),
                label + ' must not create new history',
            )
        finally:
            chat.close()
    value = json.loads(ref.read_text())
    value['used'] = False
    ref.write_text(json.dumps(value))
    chat = chatmod.BadgeChat(home, ref, Bridge(), str(model), factory).start()
    check(
        [m for m, _ in peers[-1].calls] == ['session.resume', 'session.create'],
        'missing unused draft may be replaced',
    )
    chat.close()
    value['home'] = str(root / 'another-profile')
    ref.write_text(json.dumps(value))
    before = len(peers)
    chat = chatmod.BadgeChat(home, ref, Bridge(), str(model), factory)
    try:
        chat.start()
        raise AssertionError('wrong profile accepted')
    except ValueError:
        check(len(peers) == before, 'profile mismatch fails before backend')
    finally:
        chat.close()
    ref.write_text('broken json')
    chat = chatmod.BadgeChat(home, ref, Bridge(), str(model), factory)
    try:
        chat.start()
        raise AssertionError('corrupt reference accepted')
    except ValueError:
        check(len(peers) == before, 'corrupt reference fails before backend')
    finally:
        chat.close()
    with sqlite3.connect(home / 'state.db') as db:
        db.execute(
            'CREATE TABLE sessions(id TEXT PRIMARY KEY,parent_session_id TEXT,model_config TEXT,source TEXT,end_reason TEXT)'
        )
        db.executemany(
            'INSERT INTO sessions VALUES(?,?,?,?,?)',
            [
                ('root', None, None, 'cli', 'compression'),
                ('tip', 'root', None, 'cli', None),
                ('reset', 'root', '{"_reset_from":"root"}', 'cli', None),
                ('delegate', 'root', '{"_delegate_from":"root"}', 'cli', None),
                ('tool', 'root', None, 'tool', None),
                ('new', 'tip', None, 'cli', None),
                ('cycle', 'cycle', None, 'cli', 'compression'),
            ],
        )
    check(chat.same_conversation('root', 'tip'), 'native compression preserves logical chat')
    for tip in ('reset', 'delegate', 'tool', 'new', 'cycle', 'missing'):
        check(not chat.same_conversation('root', tip), 'non-compression lineage fenced: ' + tip)
    ref.write_text(json.dumps({'home': str(home.resolve()), 'stored': 'root', 'used': True}))
    chat = chatmod.BadgeChat(
        home, ref, Bridge(), str(model), lambda receive: Peer(receive, 'tip')
    ).start()
    check(
        chat.stored == 'root' and json.loads(ref.read_text())['stored'] == 'root',
        'compression retains stable badge reference',
    )
    chat.close()
    lock = root / 'owner.lock'
    fd = chatmod.claim(lock)
    try:
        try:
            chatmod.claim(lock)
            raise AssertionError('second owner')
        except BlockingIOError:
            check(True, 'single owner')
    finally:
        os.close(fd)
    fd = chatmod.claim(lock)
    os.close(fd)
    check(True, 'lock released on exit')

# Disabled startup must return before loading Hermes, claiming locks or starting a hub.
with tempfile.TemporaryDirectory() as folder:
    disabled = Path(folder)
    env = {**os.environ, 'POCKET_AGENT': '0'}
    result = subprocess.run(
        [
            sys.executable,
            str(candidate / 'badge_chat.py'),
            '--hermes-home',
            folder,
            '--hermes-repo',
            folder,
            '--home',
            folder,
            '--voice-model',
            folder,
        ],
        env=env,
        capture_output=True,
        timeout=3,
    )
    check(
        result.returncode == 0 and not list(disabled.iterdir()), 'disabled manager performs no I/O'
    )

print(f'dedicated badge chat: {check.count} checks passed')
