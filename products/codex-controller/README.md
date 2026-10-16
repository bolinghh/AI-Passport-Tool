<p align="right"><a href="README.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Passport

A three-button pocket desktop assistant for AI Passport and a native macOS companion.
The 240 × 320 pixel-style home shows Bluetooth signal, battery, date, time and weather.
Press OK for the app list: Pomodoro, Microphone, Pixel Companion, Mini Fishing and Settings. Conversation names,
links and Codex launching have been removed from the active application.

The 0.2.1 visual foundation refines the selected [pixel-handheld concept](../../assets/images/passport-pixel-concept.png)
with chunky clock digits, shaded pixel sprites, real RSSI bars, a battery icon and
an amber full-row selection frame. Confirmation/completion panels have dedicated
space. No full-screen raster or new background animation is allocated.

## Mac reconnection (desktop 0.2.1)

Leave Passport running when restarting the device. The Rust core waits for a new
advertisement of the selected device, obtains its current BLE handle and restores
settings, time and cached weather. Microphone capture/output remain off until a
new start request on the device. Closing the window keeps recovery running in the
menu bar. Explicit Quit cancels recovery and releases the link.

The device page and menu bar show Waiting for device, Reconnecting and Syncing.
Use **Retry now** to bypass the current delay; **Stop reconnecting / Disconnect**
disables automatic recovery until you connect manually (or restart the app).
Rescan also cancels the previous connection intent, so you can select a device.
Switching targets invalidates the old attempt. Permission, pairing-denial and
protocol errors require user action followed by Retry now; bonds are never cleared.
Bluetooth off pauses scans; turning it on resumes recovery. Other scan/connect/
cleanup errors keep the command worker alive. Retry delays are 1, 2, 4, 8 and at
most 15 seconds; scan windows last 3 seconds. A paired connect, discovery and
subscription each have a 10-second deadline; cleanup has a total 5-second deadline.
First pairing retains its 90-second connect / 60-second subscription windows.

Firmware remains **0.3.0**. No new configuration keys, game-save changes, partition
writes or firmware flash are required. Windows historical builds are unaffected.
For bounded local acceptance, launch the native executable with
`--connection-log /tmp/passport-connection.jsonl`: it records connection phase,
retry count, microphone flag, process ID and visible window count only. No device
identifier, pairing value, weather, audio samples or packet contents are recorded.
This is opt-in local diagnostics, not a background telemetry service.

## Application roadmap (proposals, not implemented)

| Application | Next possibilities |
| --- | --- |
| Pixel Companion | Personality/dialogue, room decorations, growth stages, a special fishing interaction; no offline punishment |
| Mini Fishing | Three distinct ponds, per-pond guides, size records, rare-fish clues and cosmetic tackle |
| Pomodoro | Daily focus history, duration presets and companion-reward presentation |
| Microphone | Clearer level, elapsed transmission time and route status; evaluate transcription separately |
| Focus Passport | Pixel stamps for focus sessions, fishing discoveries and companion milestones |
| Today's Three | Write three small tasks on Mac; browse/check them on Passport |
| Decision Gacha | Custom lunch/task/weekend lists with a pixel draw animation |

Suggested order: connection reliability → Focus Passport and companion room →
ponds and guide expansion → Today's Three / Decision Gacha. These ideas do not
change the current app list, firmware or save schema.

## Device controls

- HOME: press OK to open Apps. UP/DOWN cycle five entries; OK opens the selection.
- Hold OK to go back one level. Releasing a hold never triggers a click.
- Pomodoro: 25-minute focus / 5-minute rest by default; UP/DOWN select primary action
  or Reset, OK executes. Running resets require confirmation (UP/DOWN cancel).
  Completion waits for confirmation before starting the next phase; no automatic loop.
  The timer continues across navigation and Bluetooth loss, and resets to idle on reboot.
- Microphone: entering never starts capture. Press OK to start/stop transmission;
  leaving, losing the link or an audio error closes capture. Reconnection never resumes it.
- Pixel Companion: UP/DOWN select Feed, Play or Outfit; OK performs it. Hold OK returns.
- Mini Fishing: OK casts, then wait for the bobber to sink and press OK during the
  1.8-second bite window. Reeling early/late misses. UP/DOWN open the field guide
  while idle or viewing results; in the guide UP/DOWN turn pages and OK returns.
  Hold OK leaves and cancels any pending cast. Waiting-game inputs never reroll fish.
- Settings: brightness, connection information, confirmed bond removal and Back.

Completion has a dismissible visual alert. The companion can send a native notification
when authorized; reminders do not play sound, including during microphone transmission.

## Companion and fishing (0.3.0)

Both apps work offline without a Mac, wall-clock sync, sound, Wi-Fi or cloud service.
The companion starts with three treats. Feed spends one treat for 5 XP; Play gives
an animated happy response without farming XP. Completing a focus phase grants
20 XP and two treats, once; rest completion grants neither. Every 100 XP adds one
level, up to level 10. Outfits cycle among unlocked styles: original, scarf at level
2, and star crown at level 4. There is no offline hunger or progress decay.

Fishing waits 2–5 seconds before a 1.8-second bite window. Six original pixel fish
have per-cast probabilities of 40%, 25%, 17%, 10%, 6% and 2%. A catch grants 8 XP
and one treat; the permanent collection is not consumed when feeding. The field
guide hides undiscovered species. Pomodoro completion or security pairing cancels
a pending cast, so a hidden game cannot time out behind an alert. Bluetooth loss
alone does not stop offline play. The two apps use the existing 150 ms render
cadence; no per-page tasks/timers, audio playback or full-screen buffers are added.

Growth, treats, outfits and six collection counts are saved only on the device in
`passport/play_v1` NVS. A bounded 3 KB storage worker coalesces changes after a quiet
interval of at least 1.2 seconds (catch results request an immediate checkpoint),
at most one enqueue per 3 seconds, and retries write errors. It does not enqueue during capture/preparation or an active cast.
Wait until the saving indicator clears before powering off; abrupt power loss can
lose recent pending rewards. A 32-byte versioned CRC record is validated before
loading. Unknown/corrupt records remain intact with a visible storage warning;
they are never silently reset or overwritten. Collection/treat counts cap at 999,
XP at 99,999. Bluetooth unpairing does not clear game progress. Casts, reactions and
menu state are transient; a reboot restores the durable progress and an idle game.

## Native Mac companion

Requires macOS 13+, native Rust, Swift Command Line Tools, CMake and Python 3.11+.
No full Xcode installation is required. In this product directory run:

```bash
bash tools/build-macos.sh
```

Open `desktop/target/release/Passport.app`. This is a local ad-hoc-signed build,
not a notarized distribution. The native SwiftUI window has Device, Microphone and
Preferences pages. The menu bar remains active when the window closes; Quit stops
output and disconnects Bluetooth. Only run one bridge instance at a time.

Install [BlackHole 2ch](https://existential.audio/blackhole/) separately. In the
Microphone page choose BlackHole 2ch, save the output and run the tone/loopback checks.
Select BlackHole 2ch as the input in your target voice application and open its voice
input manually. Use the Microphone app on Passport to transmit. Audio is not recorded.
Loopback reads only the explicitly selected BlackHole virtual input in memory.
Default system input/output devices are not changed.

Preferences searches cities using Open-Meteo; no location permission or default city
is used. Confirm a city (edit its device display alias if its glyphs are unsupported).
Weather refreshes every 30 minutes with a 10-second HTTP timeout; failures retain the
last data time and show an error. Device weather is stale after one hour. Attribution:
[Open-Meteo](https://open-meteo.com/), non-commercial API use; review commercial terms
before commercial distribution. Time/timezone follow the Mac at connection, wake,
system clock/timezone changes and every minute. A disconnected device continues
from its last synchronized clock/UTC offset; it needs the Mac for subsequent timezone
or daylight-saving changes. After device reboot, time remains unknown until synchronized.

Focus/rest durations are 1–120 minutes. Saved changes affect the next phase, not an
already running or paused phase. Brightness is stored on the device.

## Settings, protocol and compatibility

New preferences are stored in
`~/Library/Application Support/passport-codex-controller/settings.json`.
Only device identity and audio output migrate from the old `config.json`; that file
is left intact and conversation links are never copied into the new settings or logs.
The existing Bundle ID and Bluetooth bonds remain; no bond reset is automatic.

The companion and firmware require multi-app protocol v2 capability negotiation.
Old combinations fail with a bounded upgrade error; no legacy conversation-list
fallback is used. IDs for conversation messages remain retired. See
[protocol](PROTOCOL.md) and [validation](VALIDATION.md).

This iteration ships Mac only. `desktop/legacy/windows-main.rs` preserves the previous
Windows UI source as historical reference, not a buildable or compatible v2 client.
The frozen source handoff is in [HANDOFF.md](HANDOFF.md); it describes the previous version.

## Build and test

```bash
python3 tools/validate.py --host
# With ESP-IDF 5.5.3 activated:
python3 tools/validate.py --firmware
# Source archive without .git; run root gates in a disposable copy:
python3 tools/source-gate.py --mode=--all
```

Host validation also renders 45 real LVGL screen states using the firmware's
24 KB drawing pool, RGB565 and a partial buffer. It checks actual drawn clock,
icons and frames, active-font glyph coverage, and label bounds. Rendered PPM files
are under `build-host/ui-preview/`; readings there are illustrative. These checks
require the managed LVGL dependency resolved by the firmware build. To run alone:

```bash
cmake -S tests/preview -B build-host/preview
cmake --build build-host/preview --parallel 8
mkdir -p build-host/ui-preview
build-host/preview/screen_preview build-host/ui-preview
```

The Mac build runs C-compatible Rust core checks and a native Swift executable check
suite (`swift run PassportChecks` in `desktop/macos`). The latter does not require
XCTest, which is absent from Command Line Tools. Native Rust artifacts use
`desktop/target/macos13` to avoid older cached dependencies targeting a newer OS.
With a fresh direct Cargo invocation and CMake 4 set `CMAKE_POLICY_VERSION_MINIMUM=3.5`.
The product reuses the root BSP and minimal 8 MB NVS/PHY/factory partition table.
Firmware gates retain matching merged image, ELF and MAP; building never flashes.

Diagnostics in `Passport.app/Contents/MacOS/PassportDiagnostics`:

```text
PassportDiagnostics devices
PassportDiagnostics inputs
PassportDiagnostics loopback "BlackHole 2ch" "BlackHole 2ch"
PassportDiagnostics ble-check
```

Do not run BLE diagnostics while the GUI owns a connection. Distributing native
packages still requires license review, signing/notarization and hardware acceptance.
