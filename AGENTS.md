# Pocket Agent

Use [README.md](README.md) and [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md) for orientation. Read only the code and reference sections relevant to the task:

- [Behavior and protocols](docs/reference/SPEC.md) when changing application behavior, rendering or agent transport.
- [Touch calibration](docs/reference/TOUCH-CALIBRATION.md) when changing touch handling or calibration.

Maintain source in `firmware/`, `host/`, `tools/` and `tests/`. Keep generated avatar geometry, credentials, models, dependencies, runtime state and captures ignored under `.build/`. Never put personal paths, identifiers or network settings in public files.

Use four spaces, with literal tabs for Makefile recipes. The Codex Stop hook formats changed source at turn completion. Run `make format` after edits, then the relevant tests/build and `make check-format check-js check-python check-public`. Never install or upgrade dependencies without approval. Record only checks actually run; compilation, injected input and physical observations prove different things.

The C152 panel is 468 × 466, offset 6/0, rotation 0. Correct it before loading `espt-touch/record`; map every raw touch through the full shared warp. With the lanyard down, BtnA is left and BtnB is right. Preserve the 80 ms chord and 600 ms hold grammar.

For hardware work, claim `/tmp/pocket-agent-device.lock`, check port owners and stop producers plus USB/Wi-Fi hub traffic. Keep exclusive ownership through verification and release it afterward. Verify partitions, then flash app-only at `0x10000`; preserve NVS and never use a merged image at `0x0` for an update.

Commit, push, branch changes and pull requests each require explicit authorization in the current user instruction.
