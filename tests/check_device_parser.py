"""Real-device malformed-input and recorder-memory regressions. Hub must be stopped."""

import sys, time
from check_device import Device

p = Device(sys.argv[1])
p.lines(0.5)
p.send('reset')
p.lines(0.3)
p.send('assistant aaaa0001 idle 0 parser|baseline')
p.lines(0.3)
baseline = p.report()
checks = 0
p.send('voice target aaaa0001 1')
p.lines(0.2)
for command in ['voice test start', 'voice test start bbbb0002', 'voice test start aaaa0001 extra']:
    p.send(command)
    output = p.lines(0.2)
    assert 'GB_ERROR voice_test_target' in output, (command, output)
    p.send('voice')
    voice = p.lines(0.2)
    assert any(line.startswith('GB_VOICE_STATUS recording=0 sending=0 ') for line in voice), (
        command,
        voice,
    )
    assert p.report() == baseline, (command, 'Assistant state mutated')
    checks += 1
for command in [
    'ble',
    'reply begin aaaa0001 aaaaaaaaaaaaaaaa bbbbbbbbbbbbbbbb 4 2',
    'pair invalid-token',
    'voice link usb',
    'reply begin aaaa0001 0000000000000001 0000000000000002 4 0',
    'assistant aaaa0001 exploding 0 name|detail',
    'assistant aaaa0001 idle -1 name|detail',
    'assistant toolong123 idle 0 name|detail',
    'style abc',
    'style -1',
    'expr -5',
    'look nan inf',
    'act nonsense',
    'rec -1 0 0 10 1',
    'rec 1 467 465 10 1',
    'rec 1 0 0 10 1 garbage',
    'x' * 240,
]:
    p.send(command)
    output = p.lines(0.3)
    assert any(line.startswith('GB_ERROR ') for line in output), (command, output)
    assert p.report() == baseline, (command, 'Assistant state mutated')
    checks += 1
for n in [1, 120, 120, 1]:
    p.send(f'rec {n} 84 84 300 2')
    output = []
    end = time.monotonic() + 15
    while time.monotonic() < end:
        output += p.lines(0.15)
        if any(line.startswith('GB_REC_DONE ') for line in output):
            break
    assert any(line.startswith('GB_REC_START ok=1') for line in output), output
    assert any(line.startswith('GB_REC_DONE ') for line in output), output
    checks += 1
p.send('reset')
p.lines(0.3)
p.send('mood blocked')
p.lines(1.0)


def morph():
    p.send('morph')
    for line in p.lines(0.2):
        if line.startswith('GB_MORPH '):
            return {
                key: float(value) for key, value in (part.split('=') for part in line.split()[1:])
            }
    raise AssertionError('no morph evidence')


before = morph()
p.send('mood thinking')
p.lines(0.08)
after = morph()
# The original robot keeps its face. These legacy fields now report expression
# blend, mood index and whole-shell roll rather than a head-to-glyph contraction.
assert after['target'] == 1 and 0 <= after['presence'] <= 1.001, (before, after)
assert after['style'] == 1, after  # Thinking
assert before['eye_turn'] == after['eye_turn'], (before, after)
checks += 1
p.send('reset')
p.lines(0.3)
p.port.close()
print(
    f'Device parser and recorder: {checks} checks passed; malformed lines preserve Assistant state and repeated 120-frame recording succeeds.'
)
