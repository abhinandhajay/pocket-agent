"""Correlated, redacted text-only badge replies with bounded credit flow."""

import base64
import hashlib
import re
import secrets
import threading
import time
import unicodedata


def plain(text):
    text = re.sub(r'```[^\n]*\n?', '', text).replace('```', '')
    text = re.sub(r'\[([^\]]+)\]\([^)]*\)', r'\1', text)
    text = re.sub(r'(?m)^\s{0,3}#{1,6}\s+', '', text)
    text = text.replace('**', '').replace('`', '')
    for source, target in [
        ('“', '"'),
        ('”', '"'),
        ('’', "'"),
        ('‘', "'"),
        ('—', '-'),
        ('–', '-'),
        ('…', '...'),
    ]:
        text = text.replace(source, target)
    text = (
        ''.join(c for c in unicodedata.normalize('NFKD', text) if not unicodedata.combining(c))
        .encode('ascii', 'replace')
        .decode()
    )
    text = ''.join(c if c == '\n' or 32 <= ord(c) <= 126 else ' ' for c in text)
    text = re.sub(r'\n{3,}', '\n\n', text).strip()
    return (
        text if len(text) <= 4096 else text[:4051].rstrip() + '\n[More in the Hermes conversation.]'
    )


class ReplyOutput:
    def __init__(self, send, valid, redact):
        self.send, self.valid, self.redact = send, valid, redact
        self.condition = threading.Condition(threading.RLock())
        self.pending = self.active = None
        self.epoch = 0
        self.closed = False
        self.worker = None

    def reply(self, sid, ident, turn, text):
        """Send a reply scoped by the owning manager's validated session and input turn."""
        with self.condition:
            if self.closed or not turn or not isinstance(text, str):
                return
            self.cancel()
            v = {'sid': sid, 'id': ident, 'turn': turn}
            text = re.sub(
                r'-----BEGIN [^-]*PRIVATE KEY-----[\s\S]*?-----END [^-]*PRIVATE KEY-----',
                '[private key redacted]',
                text,
            )
            text = re.sub(
                r'\b(?:sk-|gh[pousr]_)[A-Za-z0-9_-]{16,}\b',
                '[credential redacted]',
                self.redact(text),
            )
            text = plain(text)
            if not text:
                return
            self._queue(v, text)

    def _queue(self, v, text):
        v.update(text=text, reply=secrets.token_hex(8), epoch=self.epoch, credits=0)
        self.pending = v
        if self.worker is None:
            self.worker = threading.Thread(target=self._run, name='badge-reply', daemon=True)
            self.worker.start()
        self.condition.notify_all()

    def receive(self, msg):
        with self.condition:
            v = self.active
            if not v or msg.get('reply') != v['reply']:
                return
            if msg.get('t') == 'reply_credit':
                count = msg.get('count')
                if type(count) is int and 0 < count <= 8 and v['credits'] + count <= 8:
                    v['credits'] += count
            elif msg.get('t') in ('reply_cancel', 'reply_result'):
                self.epoch += 1
                self.active = None
            self.condition.notify_all()

    def cancel(self):
        with self.condition:
            v = self.active
            self.epoch += 1
            self.active = self.pending = None
            self.condition.notify_all()
        if v:
            try:
                self.send({'t': 'reply_cancel', 'reply': v['reply']})
            except Exception:
                pass

    def close(self):
        self.cancel()
        with self.condition:
            self.closed = True
            self.condition.notify_all()

    def _current(self, v):
        with self.condition:
            current = not self.closed and v['epoch'] == self.epoch
        return current and self.valid(v['sid'])

    def _run(self):
        while True:
            with self.condition:
                while not self.pending and not self.closed:
                    self.condition.wait()
                if self.closed:
                    return
                v, self.pending = self.pending, None
                self.active = v
            try:
                payload = v['text'].encode('ascii')
                if not self._current(v):
                    continue
                self.send(
                    {
                        't': 'reply_begin',
                        'id': v['id'],
                        'turn': v['turn'],
                        'reply': v['reply'],
                        'text_bytes': len(payload),
                    }
                )
                for seq, offset in enumerate(range(0, len(payload), 384)):
                    deadline = time.monotonic() + 10
                    with self.condition:
                        while (
                            not v['credits']
                            and v['epoch'] == self.epoch
                            and not self.closed
                            and time.monotonic() < deadline
                        ):
                            self.condition.wait(0.1)
                        if not v['credits'] or v['epoch'] != self.epoch or self.closed:
                            raise ValueError('cancelled')
                        v['credits'] -= 1
                    if not self._current(v):
                        raise ValueError('session')
                    self.send(
                        {
                            't': 'reply_data',
                            'reply': v['reply'],
                            'seq': seq,
                            'data': base64.b64encode(payload[offset : offset + 384]).decode(),
                        }
                    )
                self.send(
                    {
                        't': 'reply_end',
                        'reply': v['reply'],
                        'bytes': len(payload),
                        'digest': hashlib.sha256(payload).hexdigest(),
                    }
                )
                del payload
            except Exception:
                try:
                    self.send({'t': 'reply_cancel', 'reply': v['reply']})
                except Exception:
                    pass
