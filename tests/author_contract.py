#!/usr/bin/env python3
"""Author tests/contract.json from compact scenarios.

Each scenario lists actions with the claims that matter. Complete expectations (every changed
field) come from reference_model.py, and any claim that disagrees with the model aborts. Edit
scenarios here, never contract.json by hand, then run tests/run.sh.
"""

import json, sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from reference_model import reduce, action_object

MOODS = [
    "idle",
    "thinking",
    "working",
    "waiting",
    "blocked",
    "done",
    "surprised",
    "sleeping",
    "happy",
    "curious",
    "excited",
    "sad",
]
PSTATES = ["none", "idle", "thinking", "tool", "error", "done", "waiting", "sleeping"]
AGES = ["fresh", "over_4s", "over_30s", "over_10m"]
state = {
    "mode": {"type": "enum", "default": "demo", "values": ["demo", "assistant"]},
    "active": {"type": "enum", "default": "none", "values": ["none", "a"]},
}
for s in "a":
    state[f"{s}_state"] = {"type": "enum", "default": "none", "values": PSTATES}
    state[f"{s}_unseen"] = {"type": "bool", "default": False}
    state[f"{s}_wince"] = {"type": "bool", "default": False}
    state[f"{s}_age"] = {"type": "enum", "default": "fresh", "values": AGES}
state |= {
    "manual_sleep": {"type": "bool", "default": False},
    "demo_style": {"type": "int", "default": 0, "min": 0, "max": 7},
    "demo_mood": {"type": "enum", "default": "idle", "values": MOODS},
    "mood": {"type": "enum", "default": "idle", "values": MOODS},
    "emit": {"type": "string", "default": "", "max_length": 16},
    "sound": {
        "type": "enum",
        "default": "none",
        "values": ["none", "pop", "start", "chime", "wince", "uhoh"],
    },
}
R = lambda i, st: {"type": "report", "id": i, "state": st}
scen = [
    (
        "a second Assistant is rejected until the owner leaves",
        {},
        [
            (R("a", "tool"), {"active": "a", "mood": "working"}),
            (R("b", "done"), {"active": "a", "mood": "working", "sound": "none"}),
            ({"type": "remove", "id": "b"}, {"active": "a"}),
            ("remove_all", {"active": "none", "mode": "demo", "a_state": "none"}),
            (R("a", "idle"), {"active": "a", "mood": "idle"}),
        ],
    ),
    (
        "first report becomes the active assistant",
        {},
        [
            (
                R("a", "idle"),
                {"active": "a", "mode": "assistant", "emit": "active a", "mood": "idle"},
            )
        ],
    ),
    (
        "working Assistant think and use tools",
        {},
        [
            (R("a", "thinking"), {"mood": "thinking"}),
            (R("a", "tool"), {"mood": "working"}),
            (R("a", "thinking"), {"mood": "thinking", "emit": ""}),
        ],
    ),
    (
        "an error wince outlasts a quick follow-up state",
        {},
        [
            (R("a", "tool"), {"mood": "working"}),
            (R("a", "error"), {"a_wince": True, "a_unseen": True, "mood": "surprised"}),
            (R("a", "thinking"), {"mood": "surprised"}),
            ("advance_5s", {"a_wince": False, "mood": "thinking"}),
        ],
    ),
    (
        "a settled error is blocked until acknowledged",
        {},
        [
            (R("a", "error"), {"mood": "surprised", "a_unseen": True}),
            ("advance_5s", {"mood": "blocked"}),
            ("enter", {"a_unseen": False, "emit": "ack a", "mood": "blocked"}),
        ],
    ),
    (
        "done celebrates then waits until acknowledged",
        {},
        [
            (R("a", "thinking"), {"mood": "thinking"}),
            (R("a", "done"), {"a_unseen": True, "mood": "done"}),
            ("advance_5s", {"mood": "waiting"}),
            ("enter", {"a_unseen": False, "emit": "ack a", "mood": "idle"}),
        ],
    ),
    (
        "an unacknowledged done settles after thirty seconds",
        {},
        [(R("a", "done"), {"mood": "done"}), ("advance_30s", {"a_unseen": True, "mood": "idle"})],
    ),
    (
        "repeated reports keep the state age",
        {},
        [
            (R("a", "done"), {"mood": "done"}),
            ("advance_5s", {"mood": "waiting"}),
            (R("a", "done"), {"a_age": "over_4s", "mood": "waiting"}),
        ],
    ),
    (
        "manual sleep overrides the assistant until any page or acknowledge",
        {},
        [
            (R("a", "thinking"), {}),
            ("hold", {"manual_sleep": True, "mood": "sleeping"}),
            (R("a", "tool"), {"mood": "sleeping"}),
            ("enter", {"manual_sleep": False, "emit": "ack a", "mood": "working"}),
            ("hold", {"manual_sleep": True}),
            ("right", {"manual_sleep": False, "emit": "active a", "mood": "working"}),
        ],
    ),
    (
        "idle Assistant doze after ten minutes",
        {},
        [
            (R("a", "idle"), {}),
            ("advance_10m", {"a_age": "over_10m", "mood": "sleeping"}),
            (R("a", "thinking"), {"a_age": "fresh", "mood": "thinking"}),
        ],
    ),
    (
        "demo mode cycles bots and states",
        {},
        [
            ("right", {"demo_style": 1}),
            ("left", {"demo_style": 0}),
            ("left", {"demo_style": 7}),
            ("enter", {"demo_mood": "happy", "mood": "happy"}),
            ("enter", {"demo_mood": "curious", "mood": "curious"}),
            ("hold", {"demo_mood": "sleeping", "mood": "sleeping"}),
            ("enter", {"demo_mood": "idle", "mood": "idle"}),
            ("hold", {"demo_mood": "sleeping", "mood": "sleeping"}),
            ("right", {"demo_style": 0, "demo_mood": "idle", "mood": "idle"}),
        ],
    ),
    (
        "transition sounds mark new sessions, run starts, completion, and errors",
        {},
        [
            (R("a", "idle"), {"sound": "pop"}),
            (R("a", "thinking"), {"sound": "start"}),
            (R("a", "tool"), {"sound": "none"}),
            (R("a", "thinking"), {"sound": "none"}),
            (R("a", "done"), {"sound": "chime"}),
            (R("a", "done"), {"sound": "none"}),
            (R("a", "tool"), {"sound": "start"}),
            (R("a", "error"), {"sound": "wince"}),
            ("advance_5s", {"sound": "uhoh", "mood": "blocked"}),
            ("advance_5s", {"sound": "none"}),
        ],
    ),
    (
        "a quick recovery from an error never says uh-oh",
        {},
        [
            (R("a", "tool"), {"sound": "pop"}),
            (R("a", "error"), {"sound": "wince"}),
            (R("a", "thinking"), {"sound": "none"}),
            ("advance_5s", {"sound": "none", "mood": "thinking"}),
        ],
    ),
    (
        "manual sleep mutes transition sounds",
        {},
        [
            (R("a", "thinking"), {"sound": "pop"}),
            ("hold", {"manual_sleep": True, "sound": "none"}),
            (R("a", "done"), {"sound": "none", "mood": "sleeping"}),
            ("enter", {"manual_sleep": False, "sound": "none"}),
        ],
    ),
    (
        "the demo screensaver is silent, even on enter",
        {"demo_mood": "surprised", "mood": "surprised"},
        [
            ("enter", {"demo_mood": "thinking", "sound": "none"}),
            ("enter", {"demo_mood": "working", "sound": "none"}),
            ("enter", {"demo_mood": "waiting", "sound": "none"}),
            ("enter", {"demo_mood": "blocked", "sound": "none"}),
            ("enter", {"demo_mood": "done", "sound": "none"}),
        ],
    ),
    (
        "a silent hub clears the stale Assistant state into the screensaver",
        {},
        [
            (R("a", "tool"), {"sound": "pop", "mood": "working"}),
            (
                "hub_silent_20s",
                {"active": "none", "mode": "demo", "mood": "idle"},
            ),
            (
                R("a", "thinking"),
                {"active": "a", "mode": "assistant", "sound": "pop", "mood": "thinking"},
            ),
        ],
    ),
    (
        "demo state cycle wraps after sad",
        {"demo_mood": "sad", "mood": "sad"},
        [("enter", {"demo_mood": "idle", "mood": "idle"})],
    ),
]
defaults = {k: v["default"] for k, v in state.items()}
out = []
for name, given, steps in scen:
    st = defaults | given
    rows = []
    for action, claim in steps:
        model = reduce(st, action_object(action))
        for k, v in claim.items():
            if model[k] != v:
                sys.exit(f"CLAIM MISMATCH {name}: {action} {k} claimed {v!r} model {model[k]!r}")
        expect = {k: model[k] for k in model if model[k] != st[k]} | claim
        rows.append({"action": action, "expect": expect})
        st = model
    sc = {"name": name, "steps": rows}
    if given:
        sc["given"] = given
    out.append(sc)
contract = {
    "schema": 1,
    "name": "pocket-agent",
    "title": "Pocket Agent",
    "hardware": {
        "board": "m5stack-stopwatch",
        "width": 468,
        "height": 466,
        "offset_x": 6,
        "offset_y": 0,
        "rotation": 0,
        "touch_map": "espt-touch/record",
    },
    "controls": {
        "left_button": "BtnA",
        "right_button": "BtnB",
        "left": "previous_or_decrease",
        "right": "next_or_increase",
        "enter": "short_both",
        "back": "hold_both_600ms",
        "rationale": "Left/right acknowledge the Assistant (demo: styles). Short both acknowledges the active assistant (demo: next state). Hold both toggles manual sleep.",
    },
    "state": state,
    "scenarios": out,
    "layout": {
        "safe_radius": 226,
        "focus_inset": 3,
        "controls": [
            {"id": "avatar_body", "bounds": [102, 130, 264, 220]},
            {"id": "session_name", "bounds": [110, 376, 248, 28]},
            {"id": "activity_line", "bounds": [116, 405, 236, 24]},
            {"id": "status_dot", "bounds": [226, 430, 16, 16]},
        ],
    },
    "visual": {
        "kind": "card",
        "eyebrow": "POCKET AGENT",
        "title": "Assistant",
        "lines": ["your turn"],
        "accent": "#1084FE",
        "scenario": "done celebrates then waits until acknowledged",
        "evidence": "reference",
    },
}
p = HERE / "contract.json"
open(p, "w").write(json.dumps(contract, indent=2) + "\n")
print("wrote", p, sum(len(s["steps"]) for s in out), "steps")
