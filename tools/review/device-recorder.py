#!/usr/bin/env python3
"""Frame-by-frame capture from the Pocket Agent firmware over USB serial (needs pyserial, Pillow).

  python3 device-recorder.py rec  <out.png> <frames> ["cmd1;cmd2"] [delay_s] [--port P] [--box x,y,size,step]
  python3 device-recorder.py shot <out.png> ["cmd1;cmd2"] [delay_s] [--port P]

`rec` asks the firmware to store <frames> cropped, downsampled frames in PSRAM at its own loop
rate (`rec <n> <x> <y> <size> <step>`), then dumps them (`recdump`) into a contact sheet with
per-frame timestamps. `shot` dumps the full 468x466 RGB565 framebuffer. Commands (e.g.
`mood thinking`, `expr 11`, `look 0 0`, `blink`, `spin`) are sent first. Stop the badge hub
first: it owns the port and may also drive a paired board over Wi-Fi. Send the hub
{"t":"pause","seconds":300} (then {"t":"resume"}) on ~/.pocket-agent/hub.sock. Opening the port can reset
the board; the tool waits for it. Pixel data is byte-swapped RGB565 as M5GFX stores it.
"""

import argparse, glob, sys, time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools'))
from serial_device import SerialDevice, rgb
from PIL import Image, ImageDraw


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("mode", choices=["rec", "shot"])
    ap.add_argument("out")
    ap.add_argument("rest", nargs="*")
    ap.add_argument("--port", default=(glob.glob("/dev/cu.usbmodem*") or [None])[0])
    ap.add_argument("--box", default="84,54,300,2")
    a = ap.parse_args()
    if not a.port:
        sys.exit("no Stopwatch serial port")
    rest = list(a.rest)
    frames = int(rest.pop(0)) if a.mode == "rec" else 0
    cmds = rest.pop(0) if rest else ""
    delay = float(rest.pop(0)) if rest else 0
    d = SerialDevice(a.port)
    d.drain(1.0)
    for c in filter(None, cmds.split(";")):
        d.send(c)
        time.sleep(0.05)
    time.sleep(delay)
    if a.mode == "shot":
        d.shot().save(a.out)
        print("saved", a.out)
        return
    x, y, size, step = map(int, a.box.split(","))
    d.send(f"rec {frames} {x} {y} {size} {step}")
    d.until(b"GB_REC_DONE", 60)
    d.clear()
    d.send("recdump")
    d.until(b"GB_REC ")
    n, sz = map(int, d.line().split())
    ts = list(map(int, d.line().split()))
    data = d.exact(n * sz * sz * 2)
    cols, tile = 10, 150
    sheet = Image.new("RGB", (cols * tile, ((n + cols - 1) // cols) * tile), "black")
    dr = ImageDraw.Draw(sheet)
    for i in range(n):
        im = Image.new("RGB", (sz, sz))
        im.putdata(rgb(data, i * sz * sz * 2, sz * sz))
        sheet.paste(im.resize((tile, tile)), ((i % cols) * tile, (i // cols) * tile))
        dr.text(
            ((i % cols) * tile + 3, (i // cols) * tile + 2),
            f"{ts[i] - ts[0]}ms",
            fill=(120, 120, 120),
        )
    sheet.save(a.out)
    print(f"frames {n} span {ts[-1] - ts[0]} ms -> {a.out}")


if __name__ == "__main__":
    main()
