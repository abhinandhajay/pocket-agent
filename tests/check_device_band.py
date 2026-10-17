#!/usr/bin/env python3
"""Actual retained-canvas checks: text replacement, paging, Assistant state clearing, dots.
Requires the exclusive fixture lock and a paused/stopped USB/Wi-Fi hub.
"""

from pathlib import Path
import sys, time

root = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(root / 'tools'))
from serial_device import SerialDevice

assert Path('/tmp/pocket-agent-device.lock').exists(), 'Claim the board fixture first'
device = SerialDevice(sys.argv[1] if len(sys.argv) > 1 else '/dev/cu.usbmodem101')
output = root / '.build/evidence/status-band'
output.mkdir(parents=True, exist_ok=True)
checks = 0


def send(line, delay=0.15):
    device.send(line)
    time.sleep(delay)


def shot(name):
    image = device.shot()
    image.save(output / (name + '.png'))
    return image


def equal(a, b, message):
    global checks
    assert a.crop((100, 372, 368, 429)).tobytes() == b.crop((100, 372, 368, 429)).tobytes(), message
    checks += 1


try:
    send('reset')
    send(
        'assistant band0001 idle 0 ABCDEFGHIJKLMNOPQRSTUVWX|a long detail with many old glyphs 12345',
        3.2,
    )
    long = shot('long-text')
    send('assistant band0001 idle 0 gjpqy|tiny gjpqy', 1)
    changed = shot('short-text')
    send('reset')
    send('assistant band0001 idle 0 gjpqy|tiny gjpqy', 3.2)
    fresh = shot('short-text-fresh')
    equal(changed, fresh, 'replacing long text must erase every old glyph and match a fresh draw')
    send('assistant band0002 idle 4 second session|different activity', 0.3)
    rejected = shot('second-owner-rejected')
    equal(fresh, rejected, 'another Assistant must not replace the owner text')
    send('act right', 1)
    acknowledged = shot('acknowledged')
    equal(fresh, acknowledged, 'acknowledging preserves Assistant text')
    assert any(acknowledged.getpixel((234, y)) != (0, 0, 0) for y in range(435, 442)), (
        'missing status dot'
    )
    checks += 1
    send('assistant-clear *', 3.2)
    empty = shot('empty-band')
    assert not any(empty.crop((100, 405, 368, 450)).tobytes()), (
        'old detail/page dots survive Assistant state clearing'
    )
    checks += 1
    print(f'Retained status band: {checks} framebuffer checks passed.')
finally:
    device.send('reset')
    device.close()
