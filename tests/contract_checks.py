"""Validate contract metadata, field bounds and round-panel layout."""

import math


from support import Checks


def validate_state(checks: Checks, contract: dict, state: dict) -> None:
    checks(isinstance(state, dict), "state must be a dictionary")
    checks(set(state) == set(contract["state"]), "state fields differ from contract")
    for name, field in contract["state"].items():
        value = state[name]
        kind = field["type"]
        if kind == "enum":
            checks(value in field["values"], f"{name} has invalid enum value {value!r}")
        elif kind == "int":
            checks(isinstance(value, int) and not isinstance(value, bool), f"{name} must be int")
            checks(field["min"] <= value <= field["max"], f"{name} outside range")
        elif kind == "bool":
            checks(isinstance(value, bool), f"{name} must be bool")
        elif kind == "string":
            checks(isinstance(value, str), f"{name} must be string")
            checks(len(value) <= field["max_length"], f"{name} is too long")
        else:
            checks(False, f"unknown field type {kind}")


def check_metadata(checks: Checks, contract: dict) -> None:
    checks(contract["schema"] == 1, "unsupported contract schema")
    checks(bool(contract["name"]), "contract name is required")
    hardware = contract["hardware"]
    checks(hardware["board"] == "m5stack-stopwatch", "wrong board")
    checks(hardware["width"] == 468 and hardware["height"] == 466, "wrong panel geometry")
    checks(hardware["offset_x"] == 6 and hardware["offset_y"] == 0, "wrong panel offsets")
    checks(hardware["rotation"] == 0, "wrong rotation")
    checks(hardware["touch_map"] == "espt-touch/record", "shared touch map missing")

    controls = contract["controls"]
    checks(controls["left_button"] == "BtnA", "BtnA must be physical left")
    checks(controls["right_button"] == "BtnB", "BtnB must be physical right")
    checks(controls["left"] == "previous_or_decrease", "wrong left action")
    checks(controls["right"] == "next_or_increase", "wrong right action")
    checks(controls["enter"] == "short_both", "wrong Enter gesture")
    checks(controls["back"] == "hold_both_600ms", "wrong Back gesture")

    visual = contract["visual"]
    checks(
        visual["scenario"] in {s["name"] for s in contract["scenarios"]},
        "visual scenario must be tested",
    )
    checks(visual["evidence"] in ("reference", "device"), "visual evidence label required")


def check_layout(checks: Checks, contract: dict) -> None:
    center_x, center_y = 234.0, 233.0
    safe_radius = contract["layout"].get("safe_radius", 226)
    inset = contract["layout"].get("focus_inset", 3)
    for control in contract["layout"]["controls"]:
        x, y, width, height = control["bounds"]
        checks(width > 0 and height > 0, f"{control['id']} has empty bounds")
        checks(
            x >= 0 and y >= 0 and x + width <= 468 and y + height <= 466,
            f"{control['id']} leaves the drawable panel",
        )
        corners = (
            (x + inset, y + inset),
            (x + width - inset, y + inset),
            (x + inset, y + height - inset),
            (x + width - inset, y + height - inset),
        )
        for point_x, point_y in corners:
            checks(
                math.hypot(point_x - center_x, point_y - center_y) <= safe_radius,
                f"{control['id']} or its focus ring leaves the round safe area",
            )
