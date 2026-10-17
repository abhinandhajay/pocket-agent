#!/usr/bin/env python3
"""Injected-device evidence: replay contract scenarios over USB serial.

Drives the real firmware through the SPEC's host protocol (`assistant`, `assistant-clear`) and
semantic `act` inputs, then compares the device's `assistant` report and emitted
GB_ACTIVE / GB_ACK lines with each step's expectation. Scenarios that need a
`given` state or 30 s / 10 min clocks are skipped (host contract only).

Stop the badge hub first; it owns the port. Usage:
  python3 check_device.py [/dev/cu.usbmodemXXXX]
"""

from __future__ import annotations

import glob
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from serial_device import SerialDevice

IDS = {"a": "aaaa0001"}
REJECTED_ID = "bbbb0002"
SLOT_BY_ID = {v: k for k, v in IDS.items()}
STYLE = {"a": 0}
LONG = {"advance_30s", "advance_10m", "hub_silent_20s"}


class Device(SerialDevice):
    def report(self) -> dict:
        self.send("assistant")
        for _ in range(20):
            for line in self.lines(0.1):
                if line.startswith("GB_ASSISTANT "):
                    return parse_assistant(line)
        raise AssertionError("no GB_ASSISTANT reply")


def parse_assistant(line: str) -> dict:
    parts = line.split()[1:]
    info = dict(p.split("=", 1) for p in parts if "=" in p)
    return {
        "active": SLOT_BY_ID.get(info["id"], "none"),
        "mood": info["mood"],
        "manual_sleep": info["manual_sleep"] == "1",
        "demo_style": int(info["demo_style"]),
        "assistant_state": info["state"],
        "assistant_unseen": info["unseen"] == "1",
    }


def command(action) -> tuple[str | None, float]:
    if isinstance(action, str):
        if action == "advance_5s":
            return None, 5.3
        if action == "remove_all":
            return "assistant-clear *", 0.3
        return f"act {action}", 0.3
    slot = action["id"]
    if slot not in IDS:
        return (
            f"assistant-clear {REJECTED_ID}"
            if action["type"] == "remove"
            else f"assistant {REJECTED_ID} idle 0 other|"
        ), 0.3
    if action["type"] == "remove":
        return f"assistant-clear {IDS[slot]}", 0.3
    return f"assistant {IDS[slot]} {action['state']} {STYLE[slot]} assistant-{slot}|", 0.3


def main() -> int:
    port = sys.argv[1] if len(sys.argv) > 1 else (glob.glob("/dev/cu.usbmodem*") or [None])[0]
    if not port:
        raise SystemExit("no Stopwatch serial port found")
    contract = json.loads(Path(__file__).with_name("contract.json").read_text())
    dev = Device(port)
    dev.lines(4.0)  # boot banner after a possible reset
    ran = skipped = checks = 0
    for scenario in contract["scenarios"]:
        if scenario.get("given") or any(
            isinstance(s["action"], str) and s["action"] in LONG for s in scenario["steps"]
        ):
            skipped += 1
            continue
        dev.send("reset")
        dev.lines(0.4)
        expected = {k: f["default"] for k, f in contract["state"].items()}
        for index, step in enumerate(scenario["steps"]):
            line, wait = command(step["action"])
            if line:
                dev.send(line)
            window = dev.lines(wait)
            emitted = [l for l in window if l.startswith(("GB_ACTIVE ", "GB_ACK "))]
            sounds = [l.split()[1] for l in window if l.startswith("GB_SOUND ")]
            expected |= step["expect"]
            got = dev.report()
            where = f"{scenario['name']} step {index} {step['action']}"
            emit = ""
            if emitted:
                verb, pid = emitted[-1].split()
                emit = f"{'active' if verb == 'GB_ACTIVE' else 'ack'} {SLOT_BY_ID.get(pid, pid)}"
            pairs = [
                ("active", got["active"], expected["active"]),
                ("mood", got["mood"], expected["mood"]),
                ("manual_sleep", got["manual_sleep"], expected["manual_sleep"]),
                ("emit", emit, expected["emit"]),
                ("sound", sounds[-1] if sounds else "none", expected["sound"]),
            ]
            if expected["mode"] == "demo":
                pairs.append(("demo_style", got["demo_style"], expected["demo_style"]))
            for slot in IDS:
                state = expected[f"{slot}_state"]
                pairs.append((f"{slot}_state", got["assistant_state"], state))
                if state != "none":
                    pairs.append(
                        (f"{slot}_unseen", got["assistant_unseen"], expected[f"{slot}_unseen"])
                    )
            for name, actual, want in pairs:
                checks += 1
                if actual != want:
                    print(f"FAIL {where}: {name}={actual!r}, expected {want!r}")
                    return 1
        ran += 1
        print(f"ok  {scenario['name']}")
    dev.send("reset")
    print(
        f"pocket-agent injected-device: {ran} scenarios, {checks} checks passed, {skipped} skipped (host-only clocks/given)."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
