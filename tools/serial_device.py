"""Bounded serial reads and retained RGB565 captures shared by device harnesses."""

import time


def rgb(data, off, count):
    if off < 0 or count < 0 or off + 2 * count > len(data):
        raise ValueError('RGB565 bounds')
    out = []
    for j in range(count):
        c = (data[off + 2 * j] << 8) | data[off + 2 * j + 1]
        out.append(
            (((c >> 11) & 31) * 255 // 31, ((c >> 5) & 63) * 255 // 63, (c & 31) * 255 // 31)
        )
    return out


class SerialDevice:
    def __init__(self, path, serial_api=None):
        if serial_api is None:
            import serial as serial_api
        self.api, self.path, self.buf = serial_api, path, b''
        self.port = self._open()

    def _open(self):
        for _ in range(40):
            try:
                return self.api.Serial(self.path, 115200, timeout=0.05)
            except self.api.SerialException:
                time.sleep(0.25)
        raise ConnectionError(f'cannot open {self.path}')

    def read(self, n=4096):
        try:
            return self.port.read(n)
        except self.api.SerialException:
            self.port.close()
            self.buf = b''
            self.port = self._open()
            return b''

    def send(self, line):
        self.port.write((line + '\n').encode())

    def lines(self, seconds):
        deadline, out = time.monotonic() + seconds, []
        while time.monotonic() < deadline:
            self.buf += self.read()
            while b'\n' in self.buf:
                line, self.buf = self.buf.split(b'\n', 1)
                out.append(line.decode(errors='replace').strip())
        return out

    def drain(self, seconds):
        self.lines(seconds)
        self.clear()

    def clear(self):
        self.buf = b''
        self.port.reset_input_buffer()

    def until(self, token, timeout=30):
        deadline = time.monotonic() + timeout
        while token not in self.buf:
            if time.monotonic() >= deadline:
                raise TimeoutError(f'waiting for {token!r}')
            if len(self.buf) > 1024 * 1024:
                raise ValueError('serial header too large')
            self.buf += self.read()
        end = self.buf.index(token) + len(token)
        out, self.buf = self.buf[:end], self.buf[end:]
        return out

    def line(self, timeout=10):
        return self.until(b'\n', timeout).decode(errors='replace').strip()

    def exact(self, n, timeout=30):
        if n < 0 or n > 468 * 466 * 2 * 32:
            raise ValueError('serial payload bounds')
        deadline = time.monotonic() + timeout
        while len(self.buf) < n:
            if time.monotonic() >= deadline:
                raise TimeoutError('serial payload')
            self.buf += self.read(min(16384, n - len(self.buf)))
        out, self.buf = self.buf[:n], self.buf[n:]
        return out

    def shot(self):
        from PIL import Image

        self.clear()
        self.send('shot')
        self.until(b'GB_SHOT ', 10)
        w, h = map(int, self.line().split())
        if (w, h) != (468, 466):
            raise ValueError('unexpected framebuffer geometry')
        data = self.exact(w * h * 2)
        self.until(b'GB_SHOT_END', 10)
        image = Image.new('RGB', (w, h))
        image.putdata(rgb(data, 0, w * h))
        return image

    def close(self):
        self.port.close()
