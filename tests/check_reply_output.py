"""Text-only final replies, correlation, redaction and cancellation; no speech calls."""

import base64, hashlib, socket, subprocess, sys, time
from pathlib import Path
from unittest.mock import patch

from support import load

m = load("m", Path(sys.argv[1]) / 'reply_output.py')
from support import Checks, wait

check = Checks()


sent = []
allowed = True
grant = True
sid = 'session'
ident = '12345678'
turn = 'a' * 16


def send(msg):
    sent.append(msg)
    if msg['t'] == 'reply_begin' and grant:
        output.receive({'t': 'reply_credit', 'reply': msg['reply'], 'count': 8})
    if msg['t'] == 'reply_data' and grant:
        output.receive({'t': 'reply_credit', 'reply': msg['reply'], 'count': 1})


def finish(text):
    sent.clear()
    output.reply(sid, ident, turn, text)
    wait(lambda: any(x['t'] == 'reply_end' for x in sent))
    begin = sent[0]
    payload = b''.join(base64.b64decode(x['data']) for x in sent if x['t'] == 'reply_data')
    end = sent[-1]
    output.receive({'t': 'reply_result', 'reply': begin['reply'], 'result': 'text'})
    return begin, payload, end


# A text reply must not invoke a speech process or any external connection.
with (
    patch.object(subprocess, 'run', side_effect=AssertionError('speech process invoked')),
    patch.object(
        socket.socket, 'connect', side_effect=AssertionError('provider connection invoked')
    ),
):
    output = m.ReplyOutput(
        send, lambda sid: allowed, lambda text: text.replace('API_KEY=secret', 'API_KEY=[redacted]')
    )
    begin, payload, end = finish('Hello **badge**. API_KEY=secret')
    check(set(begin) == {'t', 'id', 'turn', 'reply', 'text_bytes'}, 'text reply header')
    check(begin['text_bytes'] == len(payload), 'payload is exactly the display text')
    check(b'secret' not in payload and b'badge' in payload, 'redacted readable text')
    check(end['digest'] == hashlib.sha256(payload).hexdigest(), 'exact text digest')
    check(end['bytes'] == len(payload), 'exact text count')
    sent.clear()
    output.reply(sid, ident, '', 'missing input scope')
    output.reply(sid, ident, turn, None)
    time.sleep(0.05)
    check(not sent, 'missing scope and invalid display content discarded')
    grant = False
    sent.clear()
    output.reply(sid, ident, turn, 'Cancelled text')
    wait(lambda: bool(sent))
    rid = sent[0]['reply']
    output.cancel()
    output.receive({'t': 'reply_credit', 'reply': rid, 'count': 8})
    time.sleep(0.1)
    check(
        not any(x['t'] in ('reply_data', 'reply_end') for x in sent),
        'cancelled reply ignores late credits',
    )
    grant = True
    begin, payload, end = finish('word ' * 1000)
    check(len(payload) <= 4096 and b'[More in' in payload, 'long display capped with notice')
    check(
        [x['seq'] for x in sent if x['t'] == 'reply_data']
        == list(range(len([x for x in sent if x['t'] == 'reply_data']))),
        'consecutive text frames',
    )
    check(
        all(len(base64.b64decode(x['data'])) <= 384 for x in sent if x['t'] == 'reply_data'),
        'bounded text frames',
    )
    begin, payload, end = finish(
        'A credential: sk-'
        + 'x' * 32
        + '\n-----BEGIN '
        + 'PRIVATE KEY-----\nprivate fixture\n-----END PRIVATE KEY-----'
    )
    check(
        b'private fixture' not in payload and b'sk-' not in payload,
        'credential and private-key redaction',
    )
    allowed = False
    sent.clear()
    output.reply(sid, ident, turn, 'Stale session')
    time.sleep(0.1)
    check(not sent, 'invalid session discards output')
    output.close()
    allowed = True
    before = len(sent)
    output.reply(sid, ident, turn, 'closed')
    time.sleep(0.05)
    check(len(sent) == before, 'closed output cannot restart a worker')
check(not hasattr(output, '_synthesize'), 'speech generation removed')
print(
    f'Text reply output: {check.count} checks passed; speech processes and provider connections forbidden'
)
