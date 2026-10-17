# Development

`firmware/pocket_agent/` contains the C152 application. The sketch owns setup and the loop; `avatar.h`, `animation.h` and `renderer.h` own motion and drawing, `assistant.h` owns the single conversation, and settings, input and diagnostics have separate files. These modules share one translation unit in the sketch's include order. `host/hermes/` owns the badge conversation and speech input; `host/bridge/` owns USB/Wi-Fi. `tools/assets/robot_art.py` defines the original robot and 25 expressions; the standard-library generator bakes those definitions into firmware textures. Generated assets and local results stay in ignored `.build/`.

Read the [behavior reference](reference/SPEC.md) only for the feature being changed, or the [calibration reference](reference/TOUCH-CALIBRATION.md) for touch work. [Setup](SETUP.md) covers running Hermes and installing the pinned firmware toolchain.

## Tools

Use Node.js 26, Python 3.12, [uv](https://docs.astral.sh/uv/), Make, a C++17 compiler and `clang-format`. On macOS the formatter wrapper finds the copy supplied by Xcode command-line tools.

```sh
npm ci
npm ci --prefix host/bridge
export UV_PROJECT_ENVIRONMENT="$PWD/.build/python"
uv sync --locked
```

Python dependencies live in `pyproject.toml`; `uv.lock` pins their resolved versions. The default `dev` group includes avatar/capture tools and the `format` group. For portable checks alone, use `uv sync --locked --only-group format`, as CI does. Keep `UV_PROJECT_ENVIRONMENT` set when using uv so the environment stays under `.build/`. Make, the Codex hook and VS Code use `.build/python` directly. Run `uv lock` after intentional dependency changes and review the lockfile diff.

Codex uses the project [Stop hook](../.codex/hooks.json) to format changed/new source before a turn finishes. In a Codex session opened from this checkout, use `/hooks` to review and trust it once. It leaves the Git index untouched; no pre-commit hook is required. VS Code can format on save with the recommended extensions. Use four spaces; Make recipes keep literal tabs. Test/build targets never install dependencies or models and never format source. Run `make format` explicitly after edits. Avatar generation reuses its output when the local artwork, generator files and generated header are unchanged; sketch staging copies only changed files.

## Checks

```sh
make format
make test check-format check-js check-python check-public
```

Portable tests cover contract/model invariants, real isolated hubs, synthetic USB/Wi-Fi peers, badge-chat permissions/history, encrypted voice/text routing, serial parsing and C++ logic with sanitizers. They do not use the live board or external models. The pinned M5Unified button check reports a skip when its library is absent. Author scenarios in `tests/author_contract.py`; generated `contract.json` expectations are checked against the model and device replay.

For an installed Hermes backend, use its Python and source checkout:

```sh
HERMES_DISABLE_LAZY_INSTALLS=1 "$HERMES_PYTHON" tests/check_badge_chat_runtime.py host/hermes
```

This uses a private profile, synthetic recognition and a localhost provider. Keep that evidence distinct from real microphone/agent delivery. `make check-public` catches known credential patterns, syntax and broken file links; review sensitive changes before publishing.

## Device checks

Claim `/tmp/pocket-agent-device.lock`, check port owners, and stop producers and hub traffic on both transports. Keep ownership through flash, captures and final state. Follow [firmware update precautions](SETUP.md#firmware-only-for-an-unflashed-badge-or-an-update).

With the hub stopped, set `PORT` to the board's serial path and run:

```sh
uv run --no-sync tests/check_device.py "$PORT"
uv run --no-sync tests/check_device_parser.py "$PORT"
uv run --no-sync tests/check_settings_device.py "$PORT" .build/evidence/settings
uv run --no-sync tests/check_device_band.py "$PORT"
```

Replay reports host-only initial states and long clocks as skipped. These harnesses inject actions; they do not prove physical controls. Restore the normal badge service and release the lock afterward. Record run-specific results and captures under `.build/evidence/`.

Physical review should cover all moods, expressions and transitions, button holds/chords, mapped touch, microphone recognition, readable reply paging, Settings persistence/mute/brightness, and USB removal/return with Wi-Fi failover. Firmware compilation alone proves none of these.

## Avatar review

`make test` generates the robot from local source and compiles the actual avatar, animation and renderer headers into an isolated host fixture. The fixture checks every mood and expression, framebuffer bounds and retained dirty regions. It uses substitute display, clock and input hardware. This proves host rendering, not device frame rate or physical behavior.

To save host-rendered previews and an animated mood review with the already installed Pillow dependency:

```sh
make avatar-preview
```

Outputs stay in `.build/evidence/avatar/`. Review `moods.png`, `expressions.png` and `motion.gif`. The eight diagnostic palette slots all use the same robot silhouette; the Assistant defaults to amber. The screensaver changes only its mood.

The serial recorder remains available for later device review:

```sh
uv run --no-sync tools/review/device-recorder.py rec .build/thinking.png 60 "mood thinking" 0.2 --port "$PORT"
uv run --no-sync tools/review/device-recorder.py shot .build/frame.png --port "$PORT"
```

Check blinking, expression transitions, the thinking/working lights, blocked alert, bounce, touch movement, roll, and celebration. Measure `GB_FPS` outside bulk dumps; normal states must sustain at least 24 fps on the device.
