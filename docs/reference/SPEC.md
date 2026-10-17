# Pocket Agent behavior contract

Maintainer reference for behavior and wire formats. Read the relevant section when changing those features. Start with [setup](../SETUP.md) to use the badge.

## Shared platform contract

The target is the M5Stack Stopwatch with a corrected 468 × 466 drawable panel, rotation 0, x offset 6, and y offset 0.
BtnA is physical left and BtnB is physical right with the lanyard down.
Every touch consumer loads `espt-touch/record`, accepts safe version-1 and version-2 records, maps raw coordinates through the complete shared warp, and provides the shared runtime calibration flow. Hold both buttons for 600 ms during the first two seconds after boot, or issue `calibrate` over USB, to collect a replacement. A missing or invalid map opens calibration and disables avatar touch until a valid map exists. `calibrate cancel` and a calibration hold cancel without saving; the diagnostic `reset` cancels collection and returns to demo. The collection order, 51-target schedule, equal direction weighting, smoothing, safety validation, and explicit commit follow the [calibration contract](TOUCH-CALIBRATION.md). Unsafe results recollect the affected nodes while preserving other completed measurements.
All touch consumers use this shared calibration.
App-only updates preserve NVS. Normal rendering never writes the touch record. Only an explicit successful guided calibration saves a replacement; USB pairing and Wi-Fi provisioning write their own slots.

## System

```text
Hermes badge chat → private hub.sock → USB/Wi-Fi hub ⇄ badge
```

- **Producers:** the dedicated Hermes manager reports state without blocking agent execution. `POCKET_AGENT=0` disables integration; host configuration uses `POCKET_AGENT_*`; update the host and firmware together when changing the wire protocol.
- **Hub:** a single local process in `host/bridge/` owns serial, dials paired boards and routes one Assistant conversation. A producer that cannot connect starts it on demand. It exits after five minutes without clients and opens no listening network port.
- **Control utility:** `node host/bridge/cli.mjs` supports status, keychain Wi-Fi provisioning, release, forget and raw diagnostics. It never registers an Assistant, creates an agent conversation or reads credentials itself. Unsupported/malformed commands send nothing; missing-hub, disconnect, timeout, malformed/oversized response and failed results exit unsuccessfully. Raw diagnostics request an explicit bridge result and fail if no appropriate link is active. A successful diagnostic result means the line was sent to the active transport, not that physical behavior was observed.

- **Device:** renders the Assistant and drives mood from the reported state.

Badge-originated Hermes replies follow [Text replies](#text-replies): responses are paged text only; retain voice input and generate no spoken output. Transports must not change the state rules below.

The on-device [Settings screen](#settings) shows battery, Wi-Fi, agent link, voice readiness, sound, and brightness. Tap the top gear or hold left/A alone for 600 ms. Settings owns navigation until Close or a two-button hold returns to the avatar. Sound and brightness are the only editable rows; other rows report live hardware/session status. Keep its preferences separate from calibration and pairing.

Hermes push-to-talk input follows [Voice input](#voice-input): hold physical blue/right `BtnB` to listen, release to submit to the owned Hermes conversation. A 600 ms single-button hold starts recording; any shorter blue-only press pages on release and retains the reply until the actual hold threshold. Align M5Unified's blue-button click threshold with that hold; chords retain their existing grammar. Audio uses bounded authenticated encryption on USB and Wi-Fi.

## Transports: USB and Wi-Fi

- **The same line protocol runs over both.** A board may be connected by USB, by Wi-Fi, or both.
    - Per board, the hub uses one **active** link, preferring USB, and keeps the other on **standby**.
    - It writes Assistant state lines only to the active link and ignores device events arriving on a standby link.
    - When the active link changes, the hub resyncs the new one (`assistant-clear *` plus the current Assistant).
    - The voice link selector in that resync includes a strictly increasing revision persisted across hub restarts. USB and Wi-Fi can deliver earlier selections late; the board must ignore any stale selector rather than send voice Begin on a standby link. See [Voice input](#voice-input) for the wire bounds and revision requirement.
- **USB is the trusted provisioning channel.** When a board is on USB, the hub asks `hello` and reads its `GB_HELLO`:

    ```text
    GB_HELLO id=<12 hex> paired=<0|1> key=<8 hex|-> wifi=<none|saved|connected> ip=<ipv4|-> link=<wifi|-> err=<code>
    ```

    - `key` is the first 8 hex of SHA-256(token).
    - If the board is unpaired, or its key doesn't match the hub's record, the hub sends `pair <64-hex token>`.
    - The first identified USB hello must resync that connection's selected link and Assistant state even if an earlier anonymous `GB_READY` or Wi-Fi arrival already marked it active. Repeated hello for the same identity must not restart the session. The badge's voice selector must agree with the USB-preferred active link after reconnect.
    - Repeated USB health responses do not rewrite unchanged pairing data.
    - A saved/associated Wi-Fi network is not an authenticated hub link. Retry failed dials and expose only a bounded stage/error code in `devices[].dialError`, never socket error bodies or credentials. Suppress repeated identical error logs and clear the error only after mutual authentication succeeds. Verify recovery and a fresh voice/Assistant state resync after a dropped Wi-Fi link, USB removal and USB return. A hub started for hardware use needs normal LAN access; a process restricted to localhost cannot supply Wi-Fi failover.
    - The hub records the board's `ip` for dialing only when it changes, in `~/.pocket-agent/devices.json` (mode 600).
    - Provisioning commands (`wifi`, `pair`, `forget`) are accepted only from USB. Over Wi-Fi they answer `GB_ERROR usb_only`.

- **Wi-Fi credentials** live in Preferences `agent-wifi` under `ssid` and `pass`. Pairing uses `agent-link`. These are fresh settings; firmware does not read earlier project namespaces. Provision Wi-Fi and pair over USB after updating older firmware.
    - `node host/bridge/cli.mjs wifi [ssid]` reads the password from the Mac's login keychain; macOS asks the user to allow it.
    - With no argument, it discovers the Mac's Wi-Fi interface and uses its currently associated network. If macOS does not disclose that name, ask for `node host/bridge/cli.mjs wifi <ssid>` instead of choosing an unrelated remembered network.
    - It sends `wifi <base64 ssid> <base64 password>` over USB. The password is never logged or echoed, and the board confirms with `GB_WIFI saved ssid_len=<n>`.
    - Preserve the exact UTF-8 SSID, including spaces and non-ASCII characters. Reject names outside 1–32 bytes or containing control characters before accessing the keychain. Reject passwords above the board's 64-byte limit rather than truncating them.
    - Sending credentials is not connection success. Wait for the save acknowledgment, a fresh `GB_HELLO wifi=connected` with an IP, and an authenticated Wi-Fi link to that board. Poll `hello` over USB while waiting, and return a bounded failure describing whether credentials were rejected, association failed, or the hub could not dial/authenticate the board. Contain all provisioning errors; concurrent provisioning is rejected and USB loss cancels it. Allow up to 60 seconds for keychain access and 45 seconds for the wireless connection.
    - `node host/bridge/cli.mjs status` reports the board's Wi-Fi setup/association state, IP, authenticated link state, and errors. When credentials are absent, it tells the user to provision over USB. A released USB link with no authenticated Wi-Fi link reports that the board will be offline instead of claiming Wi-Fi stays up.
- **Wi-Fi link:** the board joins Wi-Fi with modem sleep enabled, advertises `pocket-agent-<last 4 hex of id>.local` and `_pocket-agent._tcp`, and listens on TCP 47837.
    - The hub dials each paired board every 3 s while it has no Wi-Fi link, alternating the last known IP and the `.local` name.
- **Handshake** (mutual HMAC-SHA256 with the pairing token as key; all hex):

    ```text
    board → HELLO <id> <nonceD:16>
    hub   → CHALLENGE <nonceH:32> <HMAC(token, "hub:" nonceD ":" nonceH)>   (or DENY unpaired)
    board → AUTH <HMAC(token, "dev:" nonceH ":" nonceD)>                   (board checks the hub's MAC first)
    hub   → OK                                                             (or DENY auth; timing-safe compare)
    ```

    - Afterwards, protocol lines flow both ways, and every `GB_*` line the board prints is mirrored to the link.
    - A newly authenticated connection replaces an old one, which covers a restarted hub.
    - Traffic is authenticated but not encrypted. It carries session names and redacted activity lines on the local network only.

- **USB never blocks the board.** The board sets its USB CDC write timeout to 0.
- **`node host/bridge/cli.mjs forget`** clears the board's pairing (`GB_FORGOT`) and the hub's records. Plugging in over USB pairs again automatically.

## Host → hub protocol

Newline-delimited JSON on the Unix socket `~/.pocket-agent/hub.sock`, overridable with `POCKET_AGENT_SOCKET`:

| Message                                      | Meaning                                                                                                                                         |
| -------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------- |
| `{"t":"hello","id","name","cwd"}`            | Register or rename a session. The name defaults to the cwd basename.                                                                            |
| `{"t":"state","id","state","detail"}`        | Report a state and a short activity line.                                                                                                       |
| `{"t":"bye","id"}`                           | The session ended.                                                                                                                              |
| `{"t":"status"}`                             | Hub replies with the port, readiness, and `assistant`, a single object or `null` when absent.                                                   |
| `{"t":"release"}`                            | Close the serial port for 30 s so a flasher can use it. Wi-Fi stays up.                                                                         |
| `{"t":"wifi","ssid"?}`                       | Send the keychain Wi-Fi credentials to the board over USB; replies `{"t":"result","ok","message"}`.                                             |
| `{"t":"forget"}`                             | Unpair every board; replies `{"t":"result",...}`.                                                                                               |
| `{"t":"pause","seconds"}` / `{"t":"resume"}` | Tests: leave the board entirely alone (no USB, no Wi-Fi) for a while. Required before `check_device.py`, because sessions respawn a killed hub. |
| `{"t":"monitor"}` / `{"t":"raw","line"}`     | Diagnostics: receive device lines / send a device command.                                                                                      |

The hub derives an Assistant ID from the last 8 alphanumerics of the session id.
It stores only printable ASCII: names are capped at 24 characters and details at 40, and `|` is replaced.
Unknown states become `idle`.
It owns one Assistant with style 0. A different session ID or owner is rejected while that conversation is registered. Control and diagnostic clients may connect independently.
When a client socket closes, its Assistant is removed, so a crashed session cannot leave a ghost.
On device `GB_READY` or port open, the hub clears the device and resends the current Assistant.

## Producer activity

The activity line is visible to anyone nearby. Before reporting, redact Bearer/Basic credentials and argument/assignment values whose key contains token, secret, password, passwd, api-key or auth. `POCKET_AGENT_DETAIL=0` restricts activity to tool names. Session name, state and a short redacted activity line use the Assistant state channel. Voice commands and their final text replies use separate encrypted, owner-scoped channels; unrelated prompts, file contents and model output never enter activity.

## Hermes badge conversation

- A supervised Mac process owns one Hermes stdio JSON-RPC backend and the `Assistant` conversation. Use the installed `tui_gateway.entry` protocol (`session.create`, `session.resume`, `prompt.submit`, `session.close`); never patch Hermes, inject into Discord, or copy another conversation's transcript.
- Save the owning Hermes home, stable stored session ID and whether a prompt has been submitted in a mode-0600 atomic reference file. Resume that exact stored ID after restart. Hermes may follow its compression continuation; accept a changed backend key only after a scoped read-only metadata check proves every parent boundary was compression and excludes reset, branch, delegate and tool children. Keep the original logical assistant ID. A runtime ID is temporary; the badge assistant ID is derived from the stored ID. An unused draft may be replaced if Hermes reports it does not exist. A missing used session, corrupt reference, or a different profile fails closed and shows Blocked; never silently start a replacement history.
- Create the initial empty conversation at startup without making a model request. Use the normal Hermes profile's configured model, tools and credentials. Disable automatic dependency installation in the owned backend. Become voice-ready before the first user command, including after host restart. Start the existing hub on demand and replay the same assistant after reconnect.
- One owned runtime and at most one badge turn may be outstanding. Validate the Assistant and readiness again at submission. Never replay a prompt after a timeout, backend crash, reconnect or ambiguous acceptance. Interruptions are visible, with a retry requiring another human command. Unrelated runtime events and completions without an outstanding badge turn cannot produce replies.
- Thinking/model events show Thinking, tool lifecycle shows Working, errors show Blocked, and successful final completion shows Done before Waiting. Redact and cap activity fields. Streamed reasoning, partial responses, tool results and error bodies never reach the badge. Only a final response belonging to the outstanding badge turn is returned as bounded, redacted, paged text; replies contain no audio.
- Hermes server requests retain their native permissions. Display a redacted request on the badge. Single/batched clarification questions can be answered with another button-held recording, one question at a time. For a command approval, accept only an explicit spoken `approve once` or `deny`, and only if that choice is offered by Hermes. Never grant session/permanent approval. Other requests (passwords, secrets, desktop UI operations and unsupported setup prompts) are declined as unsupported, never auto-approved. Do not ask the user to speak a secret.
- The manager owns an exclusive OS file lock. A second instance cannot create a second session or steal the Assistant. Shutdown closes its bridge and owned backend while preserving Hermes's stored history. Backoff on backend failure; do not expose a TCP/WebSocket port.
- `POCKET_AGENT=0` disables the manager; `POCKET_AGENT_VOICE=0` disables input. Respect existing home/socket/hub overrides. Do not log prompts, transcripts, replies, secret request bodies or credentials.

## Hub → device protocol

Text lines at 115200 over USB CDC:

- `assistant <id> <state> <style> <name>|<detail>` updates the Assistant. A different ID is rejected with `GB_ERROR assistant_busy` until the current Assistant is removed. A host and badge must use the same protocol version.
- `assistant-clear <id>` removes the matching Assistant; `assistant-clear *` clears the Assistant regardless of ID.
- USB only: `hello`, `pair`, `wifi`, and `forget` (see [Transports](#transports-usb-and-wi-fi)). `hello` is also answered over Wi-Fi.
- Diagnostics:
    - `assistant` reports `GB_ASSISTANT present=<0|1> id=<id|-> state=<state|none> unseen=<0|1> mood=<mood> manual_sleep=<0|1> demo_style=<0..7>`.
    - `act left|right|enter|hold` is semantic input.
    - `reset`, `mood <name>`, `style <n>`, `expr <n>`, `look <x> <y>`, `bounce`, `spin`, `sound <pop|start|chime|wince|uhoh>`.
    - `shot` returns the raw RGB565 framebuffer. Requested bulk dumps honor partial USB writes, yield between bounded chunks, and abandon a stalled reader after a bounded progress timeout; normal CDC writes always return to timeout zero. `morph` retains its legacy field names for diagnostic clients: `presence` is expression blend, `target` is its blend target, `style` is the mood index, and `eye_turn` is the whole-robot roll target.
    - `rec <frames> <x> <y> <size> <step>` / `recdump` records cropped frames at the loop rate for frame-by-frame review.

The device emits:

- `GB_READY` after boot.
- `GB_ACTIVE <id>` on registration or a left/right acknowledgement.
- `GB_ACK <id>` when the user acknowledges.
- Evidence lines `GB_HARDWARE`, `GB_TOUCHMAP`, `GB_SPEAKER`, `GB_FPS`, `GB_EVENT`, `GB_SOUND`.
- Link lines `GB_HELLO`, `GB_PAIRED key=`, `GB_WIFI saved ssid_len=`, `GB_FORGOT`.

## Portable state

`tests/contract.json` models the single Assistant as slot `a`.
Its fields are:

- `<slot>_state`: `none|idle|thinking|tool|error|done|waiting|sleeping`, default `none`
- `<slot>_unseen`: bool, default false. The assistant finished, failed, or asked, and the user has not paged to it or acknowledged it.
- `<slot>_wince`: bool, default false. Set by every `error` report and cleared once 2.5 s have elapsed.
- `<slot>_age`: time in the current state, `fresh|over_4s|over_30s|over_10m`

Global fields:

- `active`: `none|a`
- `mode`: `demo|assistant`
- `manual_sleep`: bool
- `demo_style`: 0–7
- `demo_mood`: mood
- `mood`: the displayed mood
- `emit`: the last device event of the action (`active x`, `ack x`, or empty)
- `sound`: the transition sound the action plays (`none|pop|start|chime|wince|uhoh`)

Clock actions `advance_5s`, `advance_30s`, and `advance_10m` stand in for elapsed time. `hub_silent_20s` stands for 20 s without hub traffic.

## Behavior

1. **Registration.** The first report registers the Assistant and emits `GB_ACTIVE`.
2. **State age.** A report that changes an Assistant's state resets its age. A repeated identical state keeps the age.
3. **Needs you.** Entering `done`, `error`, or `waiting` marks the assistant unseen. Registration does not clear unseen.
4. **Paging.** Outside Settings and replies, left and right acknowledge the Assistant and wake manual sleep. Replies still page left and right.
5. **Acknowledge.** A short both-button press clears the Assistant's unseen flag, wakes manual sleep, makes the assistant hop, and emits `GB_ACK`.
6. **Manual sleep.** Holding both for 600 ms toggles manual sleep, which forces the sleeping state until a page or acknowledge.
7. **Removal.** Removing the Assistant cancels voice/reply transfer and returns to demo mode, restoring the demo style and mood.
8. **Assistant mood** (names follow the post's lifecycle):
    - Manual sleep → sleeping.
    - A wincing assistant that is not `done` → surprised.
    - `thinking` → thinking; `tool` → working; `error` → blocked; `waiting` → waiting; `sleeping` → sleeping.
    - `done` → done (the celebration) while fresh; waiting if still unseen after 4 s; idle after 30 s.
    - `idle` or settled `done` → idle, or sleeping after 10 minutes.
9. **Hub silence:** the hub sends `ping` every 5 s on the active link. After 20 s with no line from the hub (action `hub_silent_20s`), the board removes the Assistant as if `assistant-clear *` arrived and emits `GB_EVENT hub_gone`. The hub resyncs everything when it reconnects.
10. **Demo mode is the screensaver** (no Assistant):

- Left/right cycle the eight diagnostic shell palettes and wake a sleeping demo. Every palette uses the same robot.
- Short both cycles idle → happy → curious → excited → surprised → thinking → working → waiting → blocked → done → sad → idle; from sleeping it returns to idle. It is silent.
- Without buttons or touch for a minute, it advances every 12 s through idle, happy, curious, thinking, working, excited, waiting, done, surprised (never blocked or sad), while keeping the same robot and palette.
- All sounds are muted while there are no sessions.
- Hold both toggles sleeping.

11. **Touch** uses the shared map:
    - Tap the assistant to hop.
    - Drag the assistant to move it; release springs it home with a wobble.
    - Tap elsewhere to make it glance there.
    - Any touch wakes manual sleep.
12. **Tilt:** the assistant reacts to _changes_ in orientation, never to the resting pose.
    - Downhill on the screen is (IMU `ay`, −`ax`); this mapping was measured on the device (see _Character and motion_).
    - A baseline follows it with a 2.5 s time constant. The difference (deadband 0.05 g, gain 1.5) slides the assistant up to 40 px horizontally and 30 px vertically, and steers its glances. The assistant drifts back to center as the baseline settles, so it stays centered lying flat, standing upright on its lanyard, or leaning.
    - A shake of more than 2.2 g makes it bounce and spin.

## Character and motion

One original rounded Pocket robot represents the sole Assistant. The same shell, side tabs, antenna and dark visor stay visible across all 12 moods: Idle, Thinking, Working, Waiting, Blocked, Done, Surprised, Sleeping, Happy, Curious, Excited and Sad.

- Default artwork uses a warm biscuit shell `#FFD38A`, navy visor `#1A2938`, mint eyes `#A9FFE3` and a pure black background. Eight diagnostic palette slots recolor this same silhouette; they never create another agent or character. The screensaver cycles moods without changing palettes.
- [robot_art.py](../../tools/assets/robot_art.py) authors the shell and all 25 eye expressions locally in a 256-unit canvas. The generator uses only standard Python, with no network access, website parsing, downloaded artwork, face fitting or external asset cache. Generated headers stay ignored under `.build/`.
- The rounded shell has pill-shaped ears and a short leaning antenna. The visor is centered and inset 16 artwork units from the shell on every side; subtract the same inset from the corner radius for a uniform curved border. The artwork uses solid flat colors, with antialiasing at the edges. Large rounded eyes carry the expressions. The original three-dot mouth sits inside the visor and doubles as the activity lights. All details share the robot transform and dirty bounds.
- Expression IDs 0 through 24 remain available through `expr`. The catalog includes neutral, gentle, joyful, surprised, tired, focused, both winks, busy, attentive, determined, grinning, skeptical, asleep, pondering, curious, concentrating, sparkling, puzzled, content, shy, amazed, drowsy, worried and sad eyes. Contours use 64 points per eye and bounded distance textures.
- Thinking has a tilted face and three sequential breathing lights. Working uses concentrated eyes, a quicker bob and a scanning light. Blocked keeps its worried face visible with a coral alert pulse. Done bounces with confetti and cheerful eyes. Sleeping uses closed eyes, slow breathing and a drifting `z`.
- Gaze, touch drag/squash, IMU response, blinking, winking, bounce and a whole-robot roll remain available. The face stays on the shell during a roll. There are no external avatar shapes, cylindrical eye spins, orbit ribbons or head-to-glyph contractions.
- Blinking closes to 7 percent, holds until 105 ms and reopens at 235 ms. Expression changes schedule their swap at 90 ms. Morphing interpolates capsule bounds during the blink; settled eyes use their authored contours. Eye ink is clipped to the visor.
- Motion uses small fixed integration steps, with independent targets and spring response for the shell, gaze, lids, touch and expressions. Validate interrupted state changes and the return from sleep as well as settled poses.
- **Transition sounds:** synthesized at boot (22.05 kHz mono, about −7 dBFS, speaker volume 170/255), at most one per Assistant event:

    | Event                                                                     | Sound                              |
    | ------------------------------------------------------------------------- | ---------------------------------- |
    | A new session appears                                                     | **pop** (70 ms upward sweep)       |
    | A run starts: thinking/tool entered from idle, done, waiting, or sleeping | **start** (short tick)             |
    | Done                                                                      | **chime** (C6–E6–G6 bell arpeggio) |
    | Error                                                                     | **wince** (short falling blip)     |
    | Still in error when the 2.5 s wince ends                                  | **uh-oh** (G4 → D4)                |

    Flips between thinking and tool are silent. Manual sleep and the demo screensaver mute all sounds. Review them with `sound <name>`. Every played sound emits `GB_SOUND <name>`.

- **Brightness** (battery care): any button or touch restores full brightness for a minute.
    - Screensaver: 255 for its first minute, 120 until ten minutes, then 50.
    - Assistant: dims to 120 after five minutes without work, completion or a request for attention.
    - Sleeping: 70.
    - `GB_HELLO` reports `battery=<percent> charging=<0|1>` for runtime measurements.
- **Sleeping:** closed-line expressions, drifting `z`, dim backlight.
- **Placement:** the head's home is x 234 (the round face's center). Vertically, it sits at y 204 when the name/activity band shows (assistant mode) and at y 233, centered, in demo/screensaver mode, easing between the two in about 0.3 s.
- **IMU axes** (Stopwatch, lanyard down, standing upright: ax ≈ −0.83): IMU +x points toward the lanyard (screen down); with +z out of the screen, +y points screen-left.
- **Rendering:**
    - antialiased distance fields at every scale;
    - two-level tile classification with shading only near edges;
    - dirty-rectangle pushes;
    - at least 24 fps on the device during normal states.
- **Status band:**
    - Session name, bold, centered at y 390, inside [110, 376, 248, 28].
    - Activity line at y 417, trimmed with `...` to fit [116, 405, 236, 24]. When the detail is empty, it shows the state name.
    - One Assistant status dot at y 438, inside [226, 430, 16, 16].
        - The fill is the avatar's ink, dimmed to 40 % while sleeping.
        - A ring shows state: working amber `#F5B13F`, thinking soft white `#D8D4CA`, error/blocked red `#FF4D4F` (pulsing while unseen), and finished/waiting-and-unseen pulsing blue `#1D9BF0`. An idle or acknowledged Assistant has no ring.
    - The status band appears only in assistant mode. In demo mode, a transient style or state label is shown instead.

The avatar artwork and motion targets are defined in this repository. Pocket Agent source code, documentation and artwork use the [MIT License](../../LICENSE). Dependencies retain their own licenses.

## Failure behavior

- **No hub or board:** the producer retries quietly and Hermes is unaffected. The hub rescans for an Espressif USB device (vendor `303a`) every 2 s.
- **Device resets** (opening the port may reset it): the hub resends the Assistant state on `GB_READY`.
- **Flashing:** `node host/bridge/cli.mjs release`, or `{"t":"release"}`, frees the port for 30 s, and the Assistant continues over Wi-Fi. The device must be flashed app-only at `0x10000`, which preserves NVS (touch map, Wi-Fi, and pairing). A merged image at `0x0` is a fresh install that can erase them.
- **Wi-Fi loss:** a dropped or unauthenticated link falls back to USB when present. The hub keeps redialing, and the board keeps rejoining its network every 15 s.
- **Malformed device lines** are rejected with `GB_ERROR` and do not change state. Reject invalid IDs, wire states, style indices, oversized lines, and recorder crops, dimensions, or frame counts before mutating the Assistant state or allocating memory. Diagnostic `reset` clears animation queues and restarts the demo inactivity clock. Manual mood/style selection and recording pause automatic screensaver cycling for one minute so review sequences hold their requested state. Recorder requests reuse or grow their PSRAM buffer so consecutive maximum-size recordings do not require two buffers. Recorder requests are bounded to 120 frames and 6 MiB, remain entirely inside the panel, and fail without replacing a valid recording. A different Assistant ID answers `GB_ERROR assistant_busy`.

## Voice input

Hold the physical blue/top-right button (`M5.BtnB`) to listen, release to submit. This contract defines input; [Text replies](#text-replies) defines text-only responses.

### Push-to-talk contract

- The Assistant must belong to the voice-capable badge conversation. A blue-only press held for 600 ms starts listening. Releasing before that threshold pages once, including slower 180 to 599 ms presses, and leaves the reply visible. Do not close the reply or start the microphone while the hold is pending. Align the debounced button library's click/hold threshold with 600 ms so its default 500 ms does not discard slower page taps. Chord recognition retains its 80 ms join/overlap grammar. Stop the microphone at the first debounced release. Holding both keeps the existing chord/600 ms sleep grammar; adding the left button while recording cancels that recording and consumes both releases.
- The supervised badge manager becomes ready before any user message/model request. A completed turn leaves the same conversation ready for another command. Preserve its stored identity and fail closed if a used history cannot be resumed.
- Recording is opt-in per live producer and Assistant. Demo, producers without voice support, calibration, missing host, and unavailable audio hardware never record. Show `Listening...` during actual capture, `Transcribing...` after release, and concise success/error feedback. Never put the transcript in the display activity field.
- Record mono signed 16-bit little-endian PCM at 16 kHz, at most 20 seconds / 640,000 bytes in PSRAM. Stop speaker playback and switch to the microphone before capture. Suppress assistant sounds until the microphone ends. Ignore recordings under 300 ms; reject silence before recognition. A capture limit stops listening and submits once, with no release-time duplicate.
- Preserve responsive animation, bounded nonblocking transport, normal assistant state reports, touch calibration and the sole hub serial owner. Use unique turn IDs, consecutive chunk numbers, exact byte counts, and a SHA-256 completion digest. Missing, duplicate, reordered, oversized, timed-out or cancelled frames never submit partial speech.
- Voice transport selection must agree with the hub's USB-preferred active link even when USB and Wi-Fi commands arrive in a different order. Each active-link resync carries a strictly increasing selection revision, persisted by the hub across restarts. The board ignores older, duplicate and unversioned selections after accepting a revision. A delayed Wi-Fi selection cannot override newer USB; a newer Wi-Fi selection permits real failover and cancels any in-flight take. Expose the selected link, revision, Begin-sent and challenge-received flags in `voice` diagnostics without audio, keys or challenge values.
- An injected microphone start must explicitly name its intended assistant and succeed only when that exact voice-capable Assistant is active. Reject missing, mismatched and extra targets without recording. A verification harness must register its own temporary Assistant after each resync and use the target-checked diagnostic so test audio cannot enter another conversation.
- Encrypt every PCM chunk with AES-256-GCM, including over USB. Each accepted Begin receives a fresh random 128-bit host challenge. Derive the audio key as `HMAC-SHA256(pairing_token_ascii, "pocket-agent-voice-v1:" + challenge_hex)`. A fresh random 96-bit nonce per chunk and AAD `<assistant-id>:<turn-id>:<sequence>` bind it to its destination and position. A captured prior recording cannot be replayed against a new challenge, even after a hub restart. The existing authenticated active link carries these frames; failover, reset, session removal and owner disconnect cancel in-flight audio instead of moving it into another conversation.
- The hub decrypts and validates only the active paired device, caps a turn to 20 seconds, and routes it only to the socket that owns the voice-capable Assistant. It forwards bounded chunks over its private local Unix socket, with no transcript/audio logging or persistent recordings.
- The Hermes voice worker transcribes in one bounded background worker with its existing speech library and the approved project-local model. Temporary WAV files have private permissions, live under `.build/`, and are deleted after use. Recognition errors, silent takes, stale results, closed sessions and unavailable submission fail safely without throwing into Hermes.
- Submit recognized speech exactly once as ordinary user content through the owned Hermes `prompt.submit` JSON-RPC method, preserving conversation history, tools and normal approvals. Prefix it as a spoken message so a transcript beginning with `/` cannot execute a slash command. Never use voice as an automatic tool approval. Refuse ordinary commands while a turn is busy. When Hermes awaits an approval or clarification, recordings follow the restricted answer rules in [Hermes badge conversation](#hermes-badge-conversation). A new recording cancels older pending recognition; changing the Assistant identity invalidates the old take.
- The manager's `--voice-model` option names an already downloaded local model. `POCKET_AGENT_VOICE=0` disables voice without disabling activity. Startup never downloads a model. No dependency or model auto-install is permitted by the adapter.

Wire records are newline-delimited `GB_VOICE_BEGIN <id> <turn>`, `GB_VOICE_DATA <turn> <seq> <nonce-hex> <tag-hex> <base64-ciphertext>`, `GB_VOICE_END <turn> <bytes> <sha256>`, and `GB_VOICE_CANCEL <turn>`. IDs are eight alphanumerics, turns are sixteen lowercase hex digits; PCM chunks are at most 384 bytes. Hub commands are `voice target <id> <0|1>`, `voice link <usb|wifi|none> <revision>`, `voice challenge <turn> <challenge-hex:32>`, `voice feedback <turn> <submitted|silent|error|busy>`. Revisions are decimal integers from 1 through 9007199254740991; reject malformed or extra fields. Every link selection requires a revision, including the first command after boot. The `voice` diagnostic includes `hold_ms` and `click_hold_ms`, both 600. Diagnostics `voice`, `voice test start <assistant-id>`, `voice test stop`, and `voice test cancel` report state or inject capture edges on the real microphone; they are distinct from physical button evidence.

## Text replies

Badge-originated Hermes commands receive the final assistant reply as paged text on the badge. The user removed spoken responses. Keep the existing blue-button microphone input, local transcription and normal Hermes conversation behavior. The reply producer generates no speech, invokes no speech process/provider, and sends zero audio bytes. Old speech settings or environment variables must not re-enable audio.

- Associate the submitted badge input with its session, input turn and matching Hermes model turn. Only that turn's final assistant response is eligible. Typed turns, other sessions, tool output, interim output and reasoning must never be displayed.
- The dedicated manager accepts a final only while its owned runtime session has one active badge command. It ignores unsolicited, other-session and duplicate completions, then passes the validated stored session and input turn directly to the reply sender. The dedicated manager returns a generic failure notice on a failed owned turn without provider error details.
- Redact secret-labelled values, bearer credentials, recognizable API tokens and private-key blocks. Normalize text for the supported display font, cap it to 4096 bytes, and show a truncation notice when needed. Reply text is volatile; no temporary speech files are created.
- A private owner socket can send a reply only for a completed badge input it owns. The hub routes it to that input's device on the active authenticated link. No reply content enters activity messages or generic monitoring. Owner exit, Assistant identity changes, another badge command, sleep, calibration, link changes and timeouts invalidate pending output. Late credits or results cannot revive a cancelled reply.
- Encrypt the text with AES-256-GCM. A Begin accepted by the board receives a fresh 128-bit device challenge. Derive the key as `HMAC-SHA256(pairing_token_ascii, "pocket-agent-reply-v1:" + challenge_hex)`. Per-chunk random 96-bit nonces and AAD `<assistant-id>:<input-turn>:<reply-id>:<sequence>` bind destination and order. Verify size, consecutive sequence, unique nonces and the final SHA-256 digest.
- Use eight-frame credit flow control, chunks of at most 384 plaintext bytes, bounded line buffers and bounded work per loop. USB remains nonblocking; Wi-Fi uses the same protocol. A gap or invalid completion ends transfer safely. Normal avatar states retain the existing animation budget.
- Show the reply on the existing full-screen PSRAM RGB565 canvas, black background, with round-safe text bounds and page count. In this view, left/A goes to the previous page, a quick blue/B tap goes to the next, a short two-button chord closes the view, and the existing hold sleeps/cancels. Vertical swipes page using the complete shared touch warp. Holding blue for 600 ms closes the text view and starts listening. Until that threshold, keep the reply visible; a shorter blue-only press advances one page on release. Closing the view restores the avatar. Normal Assistant/demo controls and transition sounds are separate from spoken responses.

Owner messages are `reply_begin {id, turn, reply, text_bytes}`, consecutive `reply_data {reply, seq, data}` with canonical base64, `reply_end {reply, bytes, digest}`, and `reply_cancel {reply}`. Reply payloads contain only text. Replies use sixteen lowercase hex IDs. The hub issues `reply_credit {reply, count}`, `reply_cancel {reply}`, and `reply_result {reply, result}`.

Board commands are `reply begin <assistant-id> <input-turn> <reply-id> <text-bytes>`, `reply data <reply-id> <seq> <nonce:24> <tag:32> <ciphertext-base64>`, `reply end <reply-id> <bytes> <sha256>`, and `reply cancel <reply-id>`. Evidence is `GB_REPLY_READY <reply-id> <challenge:32>`, `GB_REPLY_ACK <reply-id> <next-seq>`, `GB_REPLY_DONE <reply-id> text`, plus content-free `GB_REPLY_STATUS`. Error/cancelled results remain valid. Diagnostic microphone starts still require their explicit assistant ID. All reply records except status are private protocol traffic.

## Settings

Open Settings by tapping the gear at the top of the avatar or holding physical left/A alone for 600 ms. The left hold consumes its release. A two-button chord still follows the existing 80 ms join/overlap grammar and sleeps the avatar after 600 ms. Right/B hold remains voice input outside Settings and calibration.

Settings uses the existing full-screen PSRAM RGB565 canvas and a black background. Show six round-safe rows: Battery, Wi-Fi, Agent, Voice input, Sound, and Brightness. Refresh connection status at least once per second and battery at least every two seconds while open. Transfer only changed rows after the first full draw. Unknown readings show Unavailable rather than invented values.

- Battery shows M5Unified's approximate voltage-based percentage with a `~` prefix and its actual charging flag. Charging, discharging, and unavailable readings are distinct.
- Wi-Fi distinguishes Not set, Connecting, and association with RSSI in dBm. Association alone never claims an agent connection.
- Agent reports USB or Wi-Fi only when a live Assistant state has a fresh heartbeat and the selected link is usable. Otherwise show Offline. Voice input reports Ready, Busy, Offline, or Unavailable from the Assistant's capability and transport readiness.
- Sound toggles transition effects between On and Muted. Muting immediately stops the speaker. Replies remain text only and blue-button microphone input remains enabled.
- Brightness cycles 25%, 50%, 75%, and 100%. The chosen level caps normal, sleep, and automatic dimming. Settings stays at the chosen level while open. Default is 100% with sound On.

Left/A and right/B choose rows with wraparound. A short chord changes Sound or Brightness; other rows are read-only. A mapped row tap selects it and changes it only when editable. Hold both for 600 ms, or tap Close, to return to the avatar without sleeping or acknowledging an Assistant. All touch hit testing uses the complete shared warp. With an unavailable touch map, the left hold still works after calibration is cancelled. Voice recording is disabled while Settings owns controls. Opening Settings cancels a displayed/pending reply and never submits a command. Reject opening during microphone recording/transfer, or calibration. Calibration takes screen priority and closes Settings. Assistant state and heartbeat processing continue normally while the page is open.

Persist only explicit Sound/Brightness changes in the `agent-ui` Preferences namespace. Earlier display settings are not loaded; an unset namespace uses sound on and full brightness. Validate saved brightness against the four allowed values. No redraw, status refresh, reset diagnostic, or flash writes these preferences, Wi-Fi credentials, hub pairing, or `espt-touch/record`. Settings never exposes passwords, tokens, agent prompts, or replies.

Diagnostics over USB or authenticated Wi-Fi: `settings` emits content-free `GB_SETTINGS open=<0|1> row=<0..5> battery=<-1|0..100> charging=<-1|0|1> wifi=<none|connecting|connected> rssi=<dBm|0> agent=<offline|usb|wifi> voice=<ready|busy|offline|unavailable> sound=<on|muted> brightness=<25|50|75|100>`. `settings open` and `settings close` control only this screen. Invalid variants fail. Existing `act` diagnostics exercise its semantic navigation. Opening does not register an Assistant, acknowledge completion, or keep a crashed host alive.

## Renderer implementation notes

- Use one byte-swapped RGB565 canvas in PSRAM; M5GFX drawing APIs accept native RGB565. Push the avatar and status-band dirty rectangles separately. Copy active head and eye textures into internal RAM when they change.
- Classify 8 × 8 tiles using reach `4√2 + 2`, then 4 × 4 edge tiles using `2√2 + 2`. Flat-fill only fully classified parts; shade edge leaves. Reuse classified fields and cache solid material combinations for each frame.
- Settled eyes sample their local contour distance textures, with a vertical lid scale. Use conservative distance bounds during squash and clip eye ink to the rounded visor. Mid-transition eyes interpolate capsule bounds.
- Compose shell, visor, mint eyes, antenna and three status lights in local robot coordinates before applying the shared rotation and scale. Classify all material boundaries before flat-filling a tile.
- Keep large work arrays off the loop stack and sketch signature types in headers for Arduino prototype generation.
- Keep Wi-Fi joins/handshakes on core 0, outside rendering; guard socket replacement and buffer diagnostics per line. Normal USB CDC uses zero write timeout; requested bulk dumps temporarily allow progress and restore zero afterward.
- IMU movement subtracts a resting baseline with a 2.5 s time constant and a deadband. A stationary pose must not pull the avatar off center.
