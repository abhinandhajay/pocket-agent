# Pocket Agent

A voice interface and animated status display for Hermes on an M5Stack C152 badge. Hold the blue button for 600 ms to speak, release to send, and read the reply on the badge. Each command continues one private badge conversation.

The badge connects to a nearby Mac over USB or local Wi-Fi. Speech recognition runs locally; replies are text only. The Mac must stay awake.

## Get started

Follow [Setup](docs/SETUP.md) to connect Hermes and pair the badge. If the badge already has Pocket Agent firmware, skip the firmware section.

## Controls

- Hold blue/right for 600 ms to record. Release to submit; shorter presses acknowledge the Assistant or page replies.
- Tap left/right or swipe to page a reply. Press both briefly to close it.
- Tap the gear or hold left for 600 ms for battery, Wi-Fi, agent/voice status, sound and brightness.
- Hold both for 600 ms to sleep or wake.

For code changes, see [Development](docs/DEVELOPMENT.md). Maintained code is in `firmware/`, `host/` and `tools/`; `.build/` contains ignored dependencies, models, generated assets and local state.

One original Pocket robot shows all 12 moods and 25 eye expressions. Its artwork is defined locally in [robot_art.py](tools/assets/robot_art.py), with no external avatar download.

## License

Pocket Agent is available under the [MIT License](LICENSE). Dependencies retain their own licenses.
