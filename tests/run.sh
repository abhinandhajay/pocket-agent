#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="${POCKET_AGENT_BUILD:-$ROOT/.build}"
PYTHON="${PYTHON:-python3}"
NODE="${NODE:-node}"
HOST="$ROOT/host/bridge"
HERMES="$ROOT/host/hermes"
DEVICE="$ROOT/firmware/pocket_agent"
CONTRACT="$ROOT/tests/contract.json"
mkdir -p "$BUILD"
for file in "$HOST/hub.mjs" "$HOST/cli.mjs" "$HERMES/bridge.py" "$HERMES/badge_chat.py" "$DEVICE/pocket_agent.ino"; do
  [[ -f "$file" ]] || { echo "Missing maintained source: $file" >&2; exit 2; }
done
"$NODE" -e 'if (Number(process.versions.node.split(".")[0]) < 26) { console.error("Use Node.js 26 or newer; see docs/SETUP.md."); process.exit(2); }'
"$PYTHON" "$ROOT/tests/check_hooks.py"
"$PYTHON" "$ROOT/tests/check_config.py"
"$PYTHON" "$ROOT/tests/check_build.py"
"$PYTHON" "$ROOT/tests/check_avatar.py"
"$PYTHON" "$ROOT/tests/check_serial_device.py"
"$PYTHON" "$ROOT/tests/reference_model.py" "$CONTRACT"
for check in host cli wifi_setup wifi wifi_reconnect voice reply; do
  "$NODE" "$ROOT/tests/check_$check.mjs" "$HOST"
done
for check in badge_chat bridge voice_input reply_output; do
  "$PYTHON" "$ROOT/tests/check_$check.py" "$HERMES"
done
for check in assistant settings reply_core voice_link voice_button calibration protocol; do
  sources=("$ROOT/tests/check_$check.cpp")
  [[ "$check" != calibration ]] || sources+=("$DEVICE/touch_calibration_core.cpp")
  "${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined -I"$DEVICE" "${sources[@]}" -o "$BUILD/$check-test"
  "$BUILD/$check-test"
done
M5_BUTTON="$BUILD/toolchain/user/libraries/M5Unified/src/utility"
if [[ -f "$M5_BUTTON/Button_Class.cpp" ]]; then
  "${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined -I"$DEVICE" -I"$M5_BUTTON" "$ROOT/tests/check_voice_button_m5.cpp" "$M5_BUTTON/Button_Class.cpp" -o "$BUILD/voice-button-m5-test"
  "$BUILD/voice-button-m5-test"
else
  echo "Pinned M5Unified not present; skipped real-library button check."
fi
