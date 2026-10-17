"""Fragmented serial diagnostics/payloads, timeouts and reset recovery without a board."""

from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from serial_device import SerialDevice, rgb
from support import Checks

check = Checks()


class SerialError(Exception):
    pass


class Port:
    def __init__(self):
        self.chunks = []
        self.writes = []
        self.closed = False

    def read(self, n):
        if not self.chunks:
            return b''
        chunk = self.chunks.pop(0)
        if isinstance(chunk, Exception):
            raise chunk
        self.chunks.insert(0, chunk[n:]) if len(chunk) > n else None
        return chunk[:n]

    def write(self, data):
        self.writes.append(data)

    def close(self):
        self.closed = True

    def reset_input_buffer(self):
        self.chunks.clear()


class API:
    SerialException = SerialError

    def __init__(self):
        self.ports = []

    def Serial(self, path, baud, timeout):
        check(path == 'fixture' and baud == 115200 and timeout > 0, 'serial parameters')
        port = Port()
        self.ports.append(port)
        return port


api = API()
d = SerialDevice('fixture', api)
d.port.chunks = [
    b'noise\nGB_SH',
    b'OT 468 466\n',
    b'\xf8\x00\x07',
    b'\xe0\x00\x1fGB_SHOT_END\nnext\n',
]
check(d.until(b'GB_SHOT ') == b'noise\nGB_SHOT ', 'fragmented header')
check(d.line() == '468 466', 'header leaves payload buffered')
check(rgb(d.exact(6), 0, 3) == [(255, 0, 0), (0, 255, 0), (0, 0, 255)], 'RGB565 byte order')
check(d.until(b'GB_SHOT_END').endswith(b'GB_SHOT_END'), 'binary trailer preserved')
check(d.line() == '' and d.line() == 'next', 'trailing lines preserved')
d.send('assistant')
check(d.port.writes == [b'assistant\n'], 'command newline')
for operation in (lambda: d.line(0.001), lambda: d.exact(1, 0.001)):
    try:
        operation()
    except TimeoutError:
        check(True, 'bounded timeout')
    else:
        raise AssertionError('unbounded read')
for operation in (lambda: d.exact(-1), lambda: rgb(b'', 0, 1)):
    try:
        operation()
    except ValueError:
        check(True, 'payload bound')
    else:
        raise AssertionError('invalid payload accepted')
old = d.port
old.chunks = [SerialError('reset')]
d.buf = b'stale'
check(d.read() == b'' and old.closed and d.buf == b'', 'reset closes stale port and buffer')
check(len(api.ports) == 2, 'reopens after reset')
d.close()
check(d.port.closed, 'close')
print(f'Shared serial helper: {check.count} checks passed')
