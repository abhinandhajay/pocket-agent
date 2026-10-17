"""Observable recording/recognition/injection checks with an isolated fake STT boundary."""

import base64
import hashlib
import math
from pathlib import Path
import struct
import sys
import tempfile
import threading
import time

from support import load

module = load("module", Path(sys.argv[1]) / 'voice_input.py')
from support import Checks, wait

check = Checks()


with tempfile.TemporaryDirectory() as folder:
    results, injected, wavs = [], [], []
    gate, entered = threading.Event(), threading.Event()
    gate.set()
    enabled = True

    def transcribe(path):
        wavs.append(path)
        check(path.stat().st_mode & 0o777 == 0o600, 'private WAV')
        entered.set()
        gate.wait(2)
        return {'success': True, 'transcript': '/reset should be ordinary speech'}

    voice = module.VoiceInput(
        folder,
        folder,
        lambda ident: enabled,
        lambda ident, turn, text: injected.append((ident, text)) or True,
        lambda *r: results.append(r),
        transcribe,
    )
    ordinal = 0

    def feed(pcm=None, finish=True):
        global ordinal
        ordinal += 1
        turn = f'{ordinal:016x}'
        voice.receive({'t': 'voice_begin', 'id': '12345678', 'turn': turn, 'rate': 16000})
        if pcm is None:
            pcm = b''.join(struct.pack('<h', int(4000 * math.sin(i / 10))) for i in range(4800))
        for seq, start in enumerate(range(0, len(pcm), 384)):
            voice.receive(
                {
                    't': 'voice_data',
                    'id': '12345678',
                    'turn': turn,
                    'seq': seq,
                    'data': base64.b64encode(pcm[start : start + 384]).decode(),
                }
            )
        if finish:
            voice.receive(
                {
                    't': 'voice_end',
                    'id': '12345678',
                    'turn': turn,
                    'bytes': len(pcm),
                    'digest': hashlib.sha256(pcm).hexdigest(),
                }
            )
        return turn

    turn = feed()
    wait(lambda: results)
    check(results[-1][2] == 'submitted', 'recognized input submitted')
    check(
        injected == [('12345678', 'Spoken command: /reset should be ordinary speech')],
        'slash command not executed; exactly once',
    )
    check(all(not p.exists() for p in wavs), 'temporary recording deleted')
    feed(bytes(9600))
    wait(lambda: len(results) >= 2)
    check(results[-1][2] == 'silent', 'silent input ignored')
    check(len(injected) == 1, 'silence never injected')
    entered.clear()
    gate.clear()
    turn = feed()
    wait(entered.is_set)
    voice.receive({'t': 'voice_cancel', 'id': '12345678', 'turn': turn})
    gate.set()
    time.sleep(0.1)
    check(len(injected) == 1, 'cancel during recognition drops late result')
    enabled = False
    feed()
    check(results[-1][2] == 'busy', 'busy approval/session rejected')
    enabled = True
    turn = feed(finish=False)
    voice.receive({'t': 'voice_data', 'id': '12345678', 'turn': turn, 'seq': 0, 'data': 'AAAA'})
    check(results[-1][2] == 'error', 'out-of-order recording aborted')
    turn = feed(finish=False)
    voice.receive(
        {'t': 'voice_end', 'id': '12345678', 'turn': turn, 'bytes': 9600, 'digest': '0' * 64}
    )
    check(results[-1][2] == 'error', 'wrong completion digest aborted')
    entered.clear()
    gate.clear()
    feed()
    wait(entered.is_set)
    enabled = False
    gate.set()
    time.sleep(0.1)
    check(len(injected) == 1, 'closed/busy session cannot receive late transcript')
    voice.close()
    check(not list((Path(folder) / 'voice-tmp').glob('*.wav')), 'all recordings cleaned')
print(f'Voice input: {check.count} checks passed')
