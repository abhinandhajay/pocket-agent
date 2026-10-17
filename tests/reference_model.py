#!/usr/bin/env python3
"""Executable reading of SPEC.md's single Assistant state, used to audit contract.json.

The shared prompt-contract runner only replays scenario expectations. This
model checks that every hand-written expectation is complete and consistent
with the written rules, then fuzzes the rules for invariants.
"""

from __future__ import annotations

from contract_checks import Checks, check_metadata, check_layout, validate_state

import copy
import json
import random
import sys
from pathlib import Path

ASSISTANT_ID = "a"
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
# Demo Enter cycles these (sleeping is reached only by hold); after the last it wraps to idle.
DEMO_CYCLE = [
    "idle",
    "happy",
    "curious",
    "excited",
    "surprised",
    "thinking",
    "working",
    "waiting",
    "blocked",
    "done",
    "sad",
]
STYLE_COUNT = 8
# Transition sounds (device synthesizes them; one per action at most).
DEMO_SOUND = {
    "done": "chime",
    "blocked": "uhoh",
    "surprised": "wince",
    "thinking": "start",
    "working": "start",
}
RESTING = {"idle", "done", "waiting", "sleeping"}
NEEDS_YOU = {"done", "error", "waiting"}
WORKING = {"thinking", "tool"}
AGES = ["fresh", "over_4s", "over_30s", "over_10m"]


def defaults(contract: dict) -> dict:
    return {name: field["default"] for name, field in contract["state"].items()}


def assistant_mood(s: dict, slot: str) -> str:
    state, age = s[f"{slot}_state"], s[f"{slot}_age"]
    if s[f"{slot}_wince"] and state != "done":
        return "surprised"
    fixed = {
        "thinking": "thinking",
        "tool": "working",
        "error": "blocked",
        "waiting": "waiting",
        "sleeping": "sleeping",
    }
    if state in fixed:
        return fixed[state]
    if state == "done":
        if age == "fresh":
            return "done"
        if s[f"{slot}_unseen"] and age == "over_4s":
            return "waiting"
    return "sleeping" if age == "over_10m" else "idle"


def activate(s: dict, slot: str, by_user: bool) -> None:
    s["active"] = slot
    if slot == "none":
        return
    if by_user:
        s[f"{slot}_unseen"] = False
    s["emit"] = f"active {slot}"


def reduce(state: dict, action: dict) -> dict:
    s = copy.deepcopy(state)
    s["emit"] = ""
    s["sound"] = "none"
    kind = action["type"]

    if kind == "report":
        slot, new = action["id"], action["state"]
        if slot != ASSISTANT_ID:
            return s
        prev = s[f"{slot}_state"]
        fresh = prev == "none"
        changed = fresh or prev != new
        if fresh:
            s["sound"] = "pop"  # a new session appears
        elif changed and new == "done":
            s["sound"] = "chime"
        elif changed and new == "error":
            s["sound"] = "wince"
        elif changed and new in WORKING and prev in RESTING:
            s["sound"] = "start"  # a run begins (not thinking <-> tool flips)
        s[f"{slot}_state"] = new
        if new == "error":
            s[f"{slot}_wince"] = True
        if changed:
            s[f"{slot}_age"] = "fresh"
            if fresh:
                s[f"{slot}_unseen"] = False
            if new in NEEDS_YOU:
                s[f"{slot}_unseen"] = True
        if s["active"] == "none":
            activate(s, slot, by_user=False)
    elif kind in ("remove", "remove_all", "hub_silent_20s"):
        # hub_silent_20s: no line from the hub for 20 s (it quit, or the link is gone): the board
        # clears the stale Assistant state itself; the hub resyncs everything when it reconnects.
        if kind == "remove" and action["id"] != ASSISTANT_ID:
            return s
        s["a_state"], s["a_unseen"], s["a_wince"], s["a_age"] = "none", False, False, "fresh"
        if s["active"] != "none" and s[f"{s['active']}_state"] == "none":
            s["active"] = "none"
            s["manual_sleep"] = False
    elif kind.startswith("advance_"):
        order = {"advance_5s": 1, "advance_30s": 2, "advance_10m": 3}[kind]
        if s["a_state"] != "none":
            s["a_age"] = AGES[max(AGES.index(s["a_age"]), order)]
            if s["a_wince"] and s["a_state"] == "error":
                s["sound"] = "uhoh"
            s["a_wince"] = False
    elif s["active"] != "none":  # assistant mode buttons
        if kind in ("left", "right"):
            s["manual_sleep"] = False
            activate(s, "a", by_user=True)
        elif kind == "enter":
            s["manual_sleep"] = False
            s[f"{s['active']}_unseen"] = False
            s["emit"] = f"ack {s['active']}"
        elif kind == "hold":
            s["manual_sleep"] = not s["manual_sleep"]
    else:  # demo mode buttons
        if kind in ("left", "right") and s["demo_mood"] == "sleeping":
            s["demo_mood"] = "idle"  # a page press wakes a sleeping demo bot
        if kind == "left":
            s["demo_style"] = (s["demo_style"] - 1) % STYLE_COUNT
        elif kind == "right":
            s["demo_style"] = (s["demo_style"] + 1) % STYLE_COUNT
        elif kind == "enter":
            m = s["demo_mood"]
            s["demo_mood"] = (
                "idle"
                if m not in DEMO_CYCLE
                else DEMO_CYCLE[(DEMO_CYCLE.index(m) + 1) % len(DEMO_CYCLE)]
            )
            # Demo is the screensaver: silent, including Enter (review sounds with `sound <name>`).
        elif kind == "hold":
            s["demo_mood"] = "idle" if s["demo_mood"] == "sleeping" else "sleeping"

    if s["manual_sleep"] or s["active"] == "none":
        s["sound"] = "none"  # manual sleep and the demo/screensaver are silent
    s["mode"] = "assistant" if s["active"] != "none" else "demo"
    if s["active"] == "none":
        s["mood"] = s["demo_mood"]
    else:
        s["mood"] = "sleeping" if s["manual_sleep"] else assistant_mood(s, s["active"])
    return s


def action_object(value):
    return {"type": value} if isinstance(value, str) else dict(value)


def main() -> int:
    path = Path(sys.argv[1] if len(sys.argv) > 1 else Path(__file__).with_name("contract.json"))
    contract = json.loads(path.read_text())
    base = defaults(contract)
    checks = 0
    validation = Checks()
    check_metadata(validation, contract)
    check_layout(validation, contract)
    validate_state(validation, contract, base)
    for scenario in contract["scenarios"]:
        state = base | scenario.get("given", {})
        for index, step in enumerate(scenario["steps"]):
            action = action_object(step["action"])
            before, before_action = copy.deepcopy(state), copy.deepcopy(action)
            model = reduce(state, action)
            assert state == before and action == before_action, "model mutated its input"
            assert model == reduce(copy.deepcopy(before), copy.deepcopy(before_action)), (
                "nondeterministic model"
            )
            validate_state(validation, contract, model)
            claimed = state | step["expect"]
            if model != claimed:
                diff = {k: (claimed[k], model[k]) for k in model if model[k] != claimed[k]}
                print(f"FAIL {scenario['name']} step {index} {action}: claimed vs model {diff}")
                return 1
            state = model
            checks += 1
    # Invariants over long random sequences.
    rng = random.Random(0x17092026)
    actions = [
        {"type": t}
        for t in (
            "left",
            "right",
            "enter",
            "hold",
            "advance_5s",
            "advance_30s",
            "advance_10m",
            "remove_all",
        )
    ]
    actions += [
        {"type": "report", "id": ASSISTANT_ID, "state": st}
        for st in ("idle", "thinking", "tool", "error", "done", "waiting", "sleeping")
    ]
    actions += [{"type": "remove", "id": ASSISTANT_ID}]
    actions += [{"type": "hub_silent_20s"}]
    for _ in range(200):
        s = dict(base)
        for _ in range(200):
            action = copy.deepcopy(rng.choice(actions))
            before, before_action = copy.deepcopy(s), copy.deepcopy(action)
            input_state = s
            s = reduce(input_state, action)
            assert input_state == before and action == before_action, "model mutated its input"
            validate_state(validation, contract, s)
            assert (s["active"] == "none") == (s["a_state"] == "none"), (
                "active identity requires an Assistant"
            )
            assert s["mood"] in MOODS
            assert s["sound"] in ("none", "pop", "start", "chime", "wince", "uhoh")
            if s["a_state"] == "none":
                assert not s["a_unseen"] and not s["a_wince"], "absent Assistant keeps no flags"
            checks += 1
    print(
        f"Pocket Agent contract/model: {checks} transitions; {validation.count} schema/layout assertions passed."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
