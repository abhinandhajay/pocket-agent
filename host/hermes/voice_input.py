"""Bounded badge input, local STT, and the supported Hermes message input API."""

import array
import base64
import contextvars
import hashlib
import math
import os
from pathlib import Path
import re
import tempfile
import threading
import time
import wave


class VoiceInput:
    def __init__(self, model, home, allowed, submit, result, transcribe=None):
        self.model = model
        self.home = Path(home) / 'voice-tmp'
        self.allowed, self.submit, self.result = allowed, submit, result
        self.transcribe = transcribe or self._transcribe
        self.context = contextvars.copy_context()
        self.condition = threading.Condition(threading.RLock())
        self.take = self.pending = None
        self.epoch = 0
        self.closed = False
        self.worker = None

    def _transcribe(self, path):
        # No lazy install/download path: the approved model must already exist.
        if not (Path(self.model) / 'model.bin').is_file():
            return {'success': False}
        from tools.transcription_tools import _HAS_FASTER_WHISPER, _transcribe_local

        if not _HAS_FASTER_WHISPER:
            return {'success': False}
        return _transcribe_local(str(path), self.model)

    def cancel(self):
        with self.condition:
            self.epoch += 1
            self.take = self.pending = None

    def close(self):
        with self.condition:
            self.closed = True
            self.cancel()
            self.condition.notify()

    def receive(self, msg):
        try:
            with self.condition:
                if self.closed:
                    return
                kind, ident, turn = msg.get('t'), msg.get('id'), msg.get('turn')
                if kind == 'voice_begin':
                    self.cancel()
                    if (
                        not isinstance(ident, str)
                        or not re.fullmatch(r'[A-Za-z0-9]{8}', ident)
                        or not isinstance(turn, str)
                        or not re.fullmatch(r'[0-9a-f]{16}', turn)
                    ):
                        return
                    if not self.allowed(ident):
                        self.result(ident, turn, 'busy')
                        return
                    self.take = {
                        'id': ident,
                        'turn': turn,
                        'epoch': self.epoch,
                        'at': time.monotonic(),
                        'seq': 0,
                        'pcm': bytearray(),
                    }
                    return
                v = self.take
                if kind == 'voice_cancel':
                    self.cancel()
                    return
                if not v or v['id'] != ident or v['turn'] != turn:
                    return
                if time.monotonic() - v['at'] > 60 or not self.allowed(ident):
                    raise ValueError('stale')
                if kind == 'voice_data':
                    if msg.get('seq') != v['seq'] or type(msg.get('seq')) is not int:
                        raise ValueError('sequence')
                    data = msg.get('data')
                    if not isinstance(data, str) or len(data) > 512:
                        raise ValueError('frame')
                    pcm = base64.b64decode(data, validate=True)
                    if (
                        not pcm
                        or len(pcm) % 2
                        or len(pcm) > 384
                        or len(v['pcm']) + len(pcm) > 640000
                    ):
                        raise ValueError('size')
                    v['pcm'].extend(pcm)
                    v['seq'] += 1
                elif kind == 'voice_end':
                    if (
                        msg.get('bytes') != len(v['pcm'])
                        or len(v['pcm']) < 9600
                        or hashlib.sha256(v['pcm']).hexdigest() != msg.get('digest')
                    ):
                        raise ValueError('end')
                    self.take = None
                    self.pending = v
                    if self.worker is None:
                        self.worker = threading.Thread(
                            target=lambda: self.context.run(self._run),
                            name='badge-voice',
                            daemon=True,
                        )
                        self.worker.start()
                    self.condition.notify()
        except Exception:
            with self.condition:
                v = self.take
                self.cancel()
            if v:
                self.result(v['id'], v['turn'], 'error')

    def _run(self):
        while True:
            with self.condition:
                while not self.pending and not self.closed:
                    self.condition.wait()
                if self.closed:
                    return
                v, self.pending = self.pending, None
            path = None
            status = 'error'
            try:
                samples = array.array('h')
                samples.frombytes(v['pcm'])
                if os.sys.byteorder != 'little':
                    samples.byteswap()
                rms = math.sqrt(sum(x * x for x in samples) / len(samples))
                if rms < 80 or max(abs(x) for x in samples) < 400:
                    status = 'silent'
                else:
                    self.home.mkdir(mode=0o700, parents=True, exist_ok=True)
                    fd, name = tempfile.mkstemp(prefix='take-', suffix='.wav', dir=self.home)
                    path = Path(name)
                    with os.fdopen(fd, 'wb') as output, wave.open(output, 'wb') as wav:
                        wav.setnchannels(1)
                        wav.setsampwidth(2)
                        wav.setframerate(16000)
                        wav.writeframes(v['pcm'])
                    response = self.transcribe(path)
                    transcript = (
                        response.get('transcript', '').strip() if response.get('success') else ''
                    )
                    if response.get('success') and not transcript:
                        status = 'silent'
                    elif isinstance(transcript, str) and 0 < len(transcript) <= 8000:
                        with self.condition:
                            if (
                                not self.closed
                                and v['epoch'] == self.epoch
                                and time.monotonic() - v['at'] < 60
                                and self.allowed(v['id'])
                            ):
                                status = (
                                    'submitted'
                                    if self.submit(
                                        v['id'], v['turn'], 'Spoken command: ' + transcript
                                    )
                                    else 'error'
                                )
            except Exception:
                pass
            finally:
                if path:
                    try:
                        path.unlink()
                    except OSError:
                        pass
            with self.condition:
                if not self.closed and v['epoch'] == self.epoch:
                    self.result(v['id'], v['turn'], status)
