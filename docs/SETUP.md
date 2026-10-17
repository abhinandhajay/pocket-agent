# Setup

Use a nearby Mac with Node.js 26 and an existing Hermes installation. Hermes must have its model/tools configured, a Python environment with Faster-Whisper, and a downloaded local Whisper model directory containing `model.bin`.

## Connect Hermes

From the checkout, install the bridge dependencies and set paths for your machine:

```sh
npm ci --prefix host/bridge
export HERMES_PYTHON=/path/to/hermes/venv/bin/python
export HERMES_REPO=/path/to/hermes-agent
export HERMES_HOME="$HOME/.hermes"
export POCKET_AGENT_HOME="$HOME/.pocket-agent"
export VOICE_MODEL=/path/to/whisper-model

"$HERMES_PYTHON" host/hermes/badge_chat.py \
  --hermes-home "$HERMES_HOME" --hermes-repo "$HERMES_REPO" \
  --home "$POCKET_AGENT_HOME" --voice-model "$VOICE_MODEL"
```

Keep `node` on PATH. The manager starts the hub, creates or resumes the private conversation shown as `Assistant` without a model request, and becomes ready before your first command. Hold blue to talk. A clarification accepts another recording; a permission request accepts `approve once` or `deny` only when Hermes offers it.

Keep the runtime home and Hermes profile together to preserve pairing and conversation history. The manager never installs packages or downloads its model.

## Pair USB and Wi-Fi

Connect USB once; pairing is automatic. In another terminal using the same `POCKET_AGENT_HOME`:

```sh
node host/bridge/cli.mjs status
node host/bridge/cli.mjs wifi "<saved-2.4-GHz-ssid>"
```

Wi-Fi provisioning reads the password from the macOS keychain; approve its access prompt. USB is preferred, with authenticated Wi-Fi on standby. Unplug USB to use Wi-Fi. The board and Mac must be on the same reachable local network.

`release` frees USB for 30 seconds while Wi-Fi stays available; it is useful for checking failover. `forget` clears pairing and requires USB to pair again. `raw <diagnostic>` sends a board diagnostic.

## Start at login, optional

Generate a service using the paths above, then install it:

```sh
"$HERMES_PYTHON" tools/macos_service.py \
  --python "$HERMES_PYTHON" --hermes-repo "$HERMES_REPO" \
  --hermes-home "$HERMES_HOME" --home "$POCKET_AGENT_HOME" \
  --voice-model "$VOICE_MODEL" --node "$(command -v node)" \
  --output .build/io.pocket-agent.badge-chat.plist
mkdir -p "$HOME/Library/LaunchAgents"
cp .build/io.pocket-agent.badge-chat.plist "$HOME/Library/LaunchAgents/"
launchctl bootstrap "gui/$(id -u)" "$HOME/Library/LaunchAgents/io.pocket-agent.badge-chat.plist"
```

Stop the foreground manager first. Use one service; unload an existing badge service before replacing it. If Hermes changes its Python environment, regenerate the plist with the new interpreter and reload the service.

## Troubleshooting and configuration

For `Voice host unavailable`, check `status` for an active link and a voice-ready Assistant. If USB works but Wi-Fi does not, provision Wi-Fi over USB and check that status reports an authenticated wireless link. If the saved conversation is missing, restore its Hermes history rather than deleting the reference to create a different chat.

| Variable                                  | Purpose                                              |
| ----------------------------------------- | ---------------------------------------------------- |
| `POCKET_AGENT_HOME`                       | Private runtime home; defaults to `~/.pocket-agent`. |
| `POCKET_AGENT_SOCKET`                     | Shared socket; defaults to `hub.sock` in that home.  |
| `POCKET_AGENT_HUB_SCRIPT`                 | Alternate path to `host/bridge/hub.mjs`.             |
| `POCKET_AGENT` / `POCKET_AGENT_VOICE`     | Set `0` to disable the manager / microphone input.   |
| `POCKET_AGENT_DETAIL`                     | Set `0` to show tool names only.                     |
| `POCKET_AGENT_PORT` / `POCKET_AGENT_WIFI` | Use `none` to disable USB / `0` to disable Wi-Fi.    |
| `POCKET_AGENT_DEVICE_PORT`                | Board TCP port, normally `47837`.                    |

The CLI and Make targets also recognize an existing checkout-local `.build/hub-home`. Explicit home/socket overrides keep all processes on the same hub.

## Firmware, only for an unflashed badge or an update

Install the [development tools](DEVELOPMENT.md#tools) and Arduino CLI, then provision the pinned isolated toolchain:

```sh
make toolchain-config
arduino-cli --config-file .build/toolchain/arduino-cli.yaml core update-index
arduino-cli --config-file .build/toolchain/arduino-cli.yaml core install esp32:esp32@3.3.10
arduino-cli --config-file .build/toolchain/arduino-cli.yaml lib install M5Unified@0.2.19 M5GFX@0.2.26
make build
```

This targets ESP32-S3, OPI PSRAM, 16 MB flash and `app3M_fat9M_16MB`. The build generates the original robot artwork locally using standard Python and keeps generated geometry in `.build/`. It needs no avatar download or cache.

Update the host bridge and firmware together. The current protocol uses `assistant`/`assistant-clear`, token-only `pair`, required voice-link revisions and text-only reply headers. Network discovery and encrypted voice/reply key labels now use `pocket-agent`; an older host or firmware cannot exchange encrypted voice/replies with this version. Earlier device Wi-Fi and pairing settings are not loaded. Connect over USB to pair again, then run `node host/bridge/cli.mjs wifi` to provision Wi-Fi.

Before flashing, stop the badge manager and hub, claim the shared fixture lock and check port owners. A released USB hub can still control the board over Wi-Fi. Verify the current partition table has app0 at `0x10000` with a 3 MiB slot. Use the pinned esptool to write only `.build/firmware/pocket_agent.ino.bin` at `0x10000`, then verify its digest and unchanged partitions. Never erase NVS or write a merged image at `0x0` for an update; NVS stores touch calibration, Wi-Fi and pairing. [Device checks](DEVELOPMENT.md#device-checks) cover verification afterward.
