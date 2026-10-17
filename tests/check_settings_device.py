"""Injected settings acceptance. Requires an exclusively claimed, paused-hub board.

Usage: project-python check_settings_device.py PORT [capture-directory]
No physical controls, audio perception, recognition, or agent/provider calls.
Changes Sound/Brightness through normal actions and restores their starting values.
"""

import json
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from serial_device import SerialDevice

assert Path('/tmp/pocket-agent-device.lock').exists(), 'Claim the board fixture first'
device = SerialDevice(sys.argv[1])
output = Path(sys.argv[2]) if len(sys.argv) > 2 else None
checks = 0
original = None
events = []


def check(value, label):
    global checks
    assert value, label
    checks += 1


def command(line, duration=0.12):
    device.send(line)
    lines = device.lines(duration)
    events.extend(lines)
    return lines


def status():
    for text in command('settings', 0.2):
        if text.startswith('GB_SETTINGS '):
            return dict(x.split('=', 1) for x in text.split()[1:])
    raise AssertionError('No settings status')


def row(n):
    for _ in range((n - int(status()['row'])) % 6):
        command('act right')


def shot(name):
    if output is None:
        return
    output.mkdir(parents=True, exist_ok=True)
    image = device.shot()
    image.save(output / (name + '.png'))
    command('settings', 0.1)


try:
    command('reset', 0.5)
    original = status()
    command('settings open')
    s = status()
    check(s['open'] == '1' and s['row'] == '0', 'Open selects battery')
    check(
        s['agent'] == 'offline' and s['voice'] == 'offline',
        'Wi-Fi association does not invent an agent',
    )
    command('act enter')
    check(status()['sound'] == original['sound'], 'Battery is read-only')
    command('act left')
    check(status()['row'] == '5', 'Previous wraps to brightness')
    command('act right')
    check(status()['row'] == '0', 'Next wraps to battery')
    command('assistant setgs001 idle 2 Settings fixture|')
    command('voice target setgs001 1')
    voice = next(x for x in command('voice') if x.startswith('GB_VOICE_STATUS '))
    revision = int(dict(x.split('=', 1) for x in voice.split()[1:])['revision']) + 1
    command(f'voice link usb {revision}')
    s = status()
    check(s['agent'] == 'usb' and s['voice'] == 'ready', 'Live USB voice session is ready')
    shot('settings-overview')
    command('assistant setgs001 tool 2 Settings fixture|working')
    check(status()['voice'] == 'busy', 'Busy voice status')
    command('assistant setgs001 idle 2 Settings fixture|')
    row(4)
    command('act enter')
    s = status()
    check(s['sound'] != original['sound'], 'Sound toggles')
    shot('settings-sound')
    command('reset')
    s = status()
    check(
        s['open'] == '0' and s['sound'] != original['sound'],
        'Reset preserves explicit sound preference',
    )
    command('settings open')
    row(4)
    command('act enter')
    check(status()['sound'] == original['sound'], 'Sound restored')
    row(5)
    levels = []
    for _ in range(4):
        command('act enter')
        levels.append(int(status()['brightness']))
    check(
        set(levels) == {25, 50, 75, 100} and status()['brightness'] == original['brightness'],
        'Brightness cycle and restoration',
    )
    shot('settings-brightness')
    command('act hold')
    s = status()
    check(s['open'] == '0', 'Hold closes settings')
    report = command('assistant')
    check(
        any('manual_sleep=0' in x and 'mood=sleeping' not in x for x in report),
        'Closing did not sleep',
    )
    command('assistant setgs001 idle 2 Settings fixture|')
    command('voice target setgs001 1')
    command('settings open')
    check(
        any('GB_ERROR voice_test_target' in x for x in command('voice test start setgs001')),
        'Settings blocks voice recording',
    )
    check(
        any('GB_ERROR unknown_command' in x for x in command('settings wrong')),
        'Invalid settings variant rejected',
    )
    command('settings close')
    check(status()['open'] == '0', 'Explicit close')
    shot('settings-return-avatar')
    check(
        not any(x.startswith('GB_ACK') for x in events), 'Settings does not acknowledge a session'
    )
    print(
        json.dumps(
            {
                'ok': True,
                'checks': checks,
                'captures': str(output) if output else None,
                'final': status(),
                'scope': 'injected real board; no physical controls or audio observation',
            }
        )
    )
finally:
    if original:
        command('settings open')
        row(4)
        if status()['sound'] != original['sound']:
            command('act enter')
        row(5)
        for _ in range(4):
            if status()['brightness'] == original['brightness']:
                break
            command('act enter')
        command('settings close')
        command('assistant-clear setgs001')
    device.close()
