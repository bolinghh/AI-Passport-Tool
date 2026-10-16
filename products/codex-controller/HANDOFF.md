<p align="right"><a href="HANDOFF.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Mac development handoff

Snapshot date: 2026-10-14, Asia/Hong_Kong. Read root `AGENTS.md` first, then this
document and the product [README](README.md). This is a source handoff, not a
completed macOS release. Preserve the directory layout: firmware reuses the root
`components/bsp`, and desktop/firmware reference root font assets by relative path.

## User requirements and current priority

The product is an AI Passport BLE Codex conversation controller and virtual-mic
bridge. UP/DOWN cycle through up to 16 configured conversations. Click OK to
start audio; click again to stop. Codex voice input is opened manually. On macOS,
use BlackHole rather than developing a virtual audio driver. Keep hardware logic
in the shared BSP and product state/UI/BLE/codec tasks in the product `main`.
The device has an independent Chinese UI; never reuse the baseline demo menu.

The latest development request is to improve the desktop appearance and fix
connection attempts that briefly show an error before eventually succeeding.
The previous unresolved request is speech input: the device level changes when
speaking, but Codex voice input produces no recognized text. Complete the desktop
work and continue diagnosing the audio path; do not report either as finished.

## Exact snapshot state

| Area | State at handoff |
| --- | --- |
| Firmware | Implemented, built and flashed on Windows with explicit approval; exact bundle included below |
| Desktop | Rust/btleplug/CPAL/libopus/egui implementation; last verified Windows release passed 18 tests |
| Device BLE | Realtek USB discovery, encrypted authenticated pairing, short GUI heartbeat observation and isolated 30-second saved-bond reconnect passed |
| macOS | Native build, Bluetooth permission/pairing, BlackHole and device acceptance **NOT RUN** |
| UI improvement | Inspection and diagnosis only; existing `main.rs` UI remains in use, no redesigned release |
| Connection draft | `desktop/src/connection.rs` is present but **not imported** by `main.rs` or used by `bridge.rs`; its three tests were **not run** |
| Speech-to-Codex | User-reported failure remains unresolved; no confirmed root cause |

The last UI iteration was interrupted when the user stopped Windows Computer
Use with Escape. Apart from the unintegrated connection draft, the desktop
application was not updated. Do not confuse that draft with a working fix, and
do not claim its tests are part of the existing 18-test PASS.

The source copy has **no `.git`**, branch history or remotes. Temporary validation
copies were used on Windows; the original was not initialized, committed, pushed
or published. Relevant `demo/claude-buddy-port` patterns were consulted previously,
but that branch checkout/history is not included. Reference archives are under
`docs/reference/`, especially `shinku-chen/ble-voice-uplink-and-pairing.md`.

## Findings to carry forward

### Connection and desktop UI

- Startup auto-connects `Config.peripheral`. UI connection controls currently
  depend only on `Status.connected`; they remain clickable during the attempt.
  Repeated `Command::Connect` requests can queue and trigger another connection
  when the first session begins. Same-device requests are not ignored in `session`.
- New attempts do not consistently clear previous errors. Ordinary disconnected
  state displays generic pairing-failure copy. Introduce explicit scanning,
  connecting/pairing, syncing, ready, disconnecting and failure states; deduplicate
  requests and reject stale completions. The draft proposes epoch-based transitions
  but still needs integration, cancellation/cleanup design and actual tests.
- The incumbent native screenshot showed a light default-style interface with
  small controls, crowded headings and a large empty area. `App::new` calls
  `set_visuals(dark())`, but the observed result was light; inspect eframe theme
  handling. Improve hierarchy, spacing, readable typography, status/progress,
  empty/error states and audio-route guidance while keeping all three functions.
- A previous GUI/CLI overlap produced an authentication error. Run one BLE
  connection owner at a time; do not interpret a competing test as RF failure.

The Impeccable context launcher on Windows failed because its engine/cache was
unavailable. No formal product/design files or approved new UI were created.
That local tooling issue is not a Mac prerequisite and should not block work.

### Audio diagnosis

- User confirmed the missing result is **Codex transcription**, rather than
  explicitly reporting a missing bridge or Windows-input level.
- Windows bridge configuration selected `CABLE Input`; both `CABLE Input` output
  and `CABLE Output` input endpoints were present. No personal config is included.
- Device logs showed notifications approximately every 60 ms while capture was
  active, with device level changes reported by the user. This does not establish
  decoded audio, virtual input signal, intelligibility or correct Codex input.
- A prior local VB-CABLE test-tone loopback passed at 48 kHz/two channels/F32,
  approximately 995.3 Hz, with zero final silence peak. It did not include BLE
  speech and was not repeated during the latest transcription failure.
- After GUI use the audio task reported a historical minimum stack margin of
  **452 bytes** from a 24 KiB stack. No panic was observed, but load review is
  required before audio/stability acceptance. Do not arbitrarily enlarge every
  stack on the no-PSRAM board. Investigate Opus call paths and RAM budget.
- Inspect `main/capture.c`, `main/link.c`, `desktop/src/bridge.rs` and
  `desktop/src/audio.rs`. Check validated stream IDs, packet loss, decoder result,
  output gate, queue priming/resampling, chosen virtual endpoints and Codex's
  manually selected microphone. Measure levels without saving private speech.

### BLE repair already implemented

Firmware previously treated Windows CCCD `0 -> 0` initialization as an unsubscribe.
It now drops only on `1 -> 0`, with C regression coverage for all four transitions.
Encrypted/authenticated NUS, Secure Connections and bonding remain required.
Windows custom `ProvidePin` pairing uses the bridge's six-digit dialog and a
native `GattSession` lease retained for the complete protocol session, including
bonded reconnects. Keep timeout/cancellation/lease cleanup intact. macOS uses
CoreBluetooth/system pairing; the Windows implementation is not Mac acceptance.

The Windows PC's onboard Intel adapter lacked an external antenna and scanned
approximately -98 to -104 dBm. A Realtek USB Bluetooth 5.0 adapter initially had
code 31 because Windows allowed only one active radio. After authorized switching,
Realtek had code 0, BLE/central support and approximately -66 dBm Passport RSSI.
The user chose to keep Realtek enabled/Intel disabled. This is Windows-specific
evidence, not a requirement to disable anything on the Mac.

## Included material and exact firmware

The ZIP keeps first-party source, root BSP/baseline tests/gates, documents/reference
archives, canonical repository skills, required font assets/licenses and lockfiles.
It includes only the exact currently flashed firmware/debug bundle from generated
output, at:

`build/firmware/1f15a1fdd07d76de043dbc037e460e2673b86bc4d50e3338a19ca09f98bf45e9/`

This path is relative to the product directory. Verify it from repository root:

```bash
python3 tools/archive_firmware.py verify products/codex-controller/build/firmware/1f15a1fdd07d76de043dbc037e460e2673b86bc4d50e3338a19ca09f98bf45e9
```

| Identity | Value |
| --- | --- |
| Merged image SHA-256 | `1f15a1fdd07d76de043dbc037e460e2673b86bc4d50e3338a19ca09f98bf45e9` |
| Matching ELF SHA-256 | `730d61dc2b413ac776028ee41467f4d45435118c22cface4feff35a207d6aaf1` |
| Matching MAP SHA-256 | `c242434ec68123d2506b362970757c3d344b8c432fe400b6985eae055bdf6878` |
| Merged size / offset | 1,787,792 bytes / 0x0 |
| Last Windows executable SHA-256 (binary omitted) | `7bc79b6adb56f5f3a24ebf7517fa1d8894725d27e23e1fcce33a85a78c876a51` |

Target: ESP32-C3, ESP-IDF 5.5.3, 8 MB Flash, no PSRAM, DIO/80 MHz. NVS
0x9000/0x6000, PHY 0xf000/0x1000, factory 0x10000/0x7f0000. Match logs to this
ELF before decoding crashes. Historical Windows COM3/address details are not Mac
device selection. Detect the Mac port and current BLE identifier afresh.

The ZIP excludes build caches, downloaded Managed Components, Cargo target,
node_modules, Windows toolchains/executables, old binaries, temporary Git copies,
raw runtime logs, pairing codes/keys, user settings and personal conversation
links. Dependencies must be downloaded on the Mac. `HANDOFF_MANIFEST.json` at ZIP
root records each included file's hash/size, executable mode and exclusions;
the adjacent `.sha256` verifies the complete ZIP.

## Mac start and validation

1. Extract one top-level `ai-passport-codex-controller/` directory. Add that folder
   as the Codex project and read `AGENTS.md`. The AI must verify/install all five
   required `passport-*` skills from `skills/` itself; `python3
   tools/install_passport_skills.py --install` is one available method, followed
   by `--check` and verification that Codex can discover the skills. Existing user
   skills/configuration must be preserved.
2. Detect `uname -m`; use native Apple Silicon or Intel Rust. Prepare Python
   3.11+, Rust, Xcode Command Line Tools and CMake for the desktop build. Use
   `passport-setup` and `docs/development/engineering/environment-setup.md` for
   exact ESP-IDF 5.5.3 and ESP32-C3 toolchain setup. Do not copy Windows paths or
   assume the Mac already has those tools. System installs/permissions follow
   the active Mac environment's approval rules.
3. From the product directory, run:

   ```bash
   bash tools/build-macos.sh
   cargo clippy --locked --all-targets --manifest-path desktop/Cargo.toml -- -D warnings
   ```

   Launch `desktop/target/release/Passport Codex Controller.app`, which carries
   Bluetooth/microphone purpose strings in `Info.plist`. Grant required permissions
   through the Mac's normal user flow. This is a local unsigned app, not a signed
   or notarized release. Install BlackHole separately if not present; bridge output
   and Codex microphone input both select its corresponding endpoint.
4. Product host/firmware gates, from the product directory:

   ```bash
   python3 tools/validate.py --host
   python3 tools/validate.py --firmware
   ```

   Firmware requires activated ESP-IDF 5.5.3. For the unchanged complete repository
   gate on this source archive, from repository root use:

   ```bash
   python3 products/codex-controller/tools/source-gate.py --mode=--static
   python3 products/codex-controller/tools/source-gate.py --mode=--firmware
   python3 products/codex-controller/tools/source-gate.py --mode=--all
   ```

   This initializes temporary Git metadata only in disposable validation copies,
   preserving the original source copy. Network is needed for dependencies and
   actionlint when not already installed. The root firmware gate builds the
   baseline demo; never flash that output instead of the product firmware.
   The root host gate currently passes GNU linker flags such as
   `-Wl,--gc-sections`; inspect native Mac linker compatibility and port the
   invocation if needed, preserving the tests rather than suppressing them.
5. Apply the [acceptance matrix](VALIDATION.md) on macOS. First establish one
   secure connection; test the virtual route using the named BlackHole input/output
   with `loopback`, then verify actual device speech and manual Codex voice input.
   Keep one bridge instance active. Do not promise microphone input creates text
   automatically outside an active Codex voice-input session.

## Authorization and reporting

No flash, full-chip erase, bond reset, driver change, commit, push or publication
is authorized just by receiving this archive. Earlier Windows flashing approval
does not approve a new Mac flash. Before flashing, bind the exact verified image
to the freshly detected target/port and confirm its stored-data impact. A merged
0x0 flash can reset NVS/bonds; no original-firmware backup is a prerequisite.

Historical results: Windows firmware/desktop **Build PASS**, product C tests and
18 Rust **Host tests PASS**, **Device tests PASS only for the recorded BLE/UI
checks**. Native macOS build/tests and speech acceptance are **NOT RUN**. The
unfinished draft and requested redesign are **not** part of those PASS results.
For this handoff, verify archive CRC, SHA-256, relative dependencies, paired docs
and artifact identities; do not describe packaging as a new firmware build.

Report `Build`, `Host tests`, `Device tests`, and `Unverified` separately after
the Mac's own work. Update paired authoritative records; avoid copying stale
Windows PASS labels into Mac results. Keep user audio/configuration private.

## Suggested starting instruction for Mac Codex

To reproduce the source/debug package on the source machine:
`python3 products/codex-controller/tools/package-handoff.py --date 20261014`.
It preserves existing ZIPs and verifies CRC, per-file hashes and Unix modes.

```text
Read AGENTS.md and products/codex-controller/HANDOFF.md. Verify/install the five
required Passport skills and prepare the native Mac environment. Preserve the
source snapshot and unintegrated connection.rs draft. Complete the desktop UI
improvement and connection request/state fixes, then diagnose why device speech
does not reach Codex transcription using actual BLE, decoder and virtual-input
levels. Run native tests/builds and the required gates; report Windows historical
evidence separately from Mac results. Do not flash, reset bonds, commit, push or
publish without fresh applicable authorization.
```
