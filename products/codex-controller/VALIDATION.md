<p align="right"><a href="VALIDATION.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Validation and acceptance

## 2026-10-16 — desktop 0.2.1 reconnection repair (firmware remains 0.3.0)

| Field | Result |
| --- | --- |
| Build | PASS: Rust static core, native Swift package, signed local Mac app; repository complete gate including its baseline firmware build |
| Host tests | PASS: 3 product C groups, 45 actual LVGL render states, 32 Rust tests, Swift native checks and strict Clippy with warnings denied |
| Device tests | PASS for the observed recovery cases below; no firmware flash or data erase |
| Unverified | Fresh pairing denial/permission prompts on hardware, two-device target switching, physical power-off/on (ROM-mode absence was used), recovery across Mac sleep, actual macOS 13 runtime and renewed speech-transcription quality |

Baseline: the existing core reached readiness, then a confirmed device reset left
it in Failed after a heartbeat write timeout and a 5-second cleanup delay. It
stayed alive without reconnecting until the diagnostic ended. Dependency source
inspection independently shows CoreBluetooth removes its internal peripheral on
disconnect while btleplug's outer adapter cache can retain the old handle.

The final native app process remained unchanged for ten device resets. Measured
reset-command start to full protocol/settings readiness was **5.31, 4.13, 3.88, 4.14, 4.49, 4.44, 3.99, 4.55, 4.18 and 4.23 seconds**. These measurements include
boot time and satisfy the 15-second post-advertising target in this environment;
they are not a general latency guarantee in poor RF conditions. Every readiness
snapshot reported the microphone off. Earlier builds also passed two complete
ten-reset runs; the final run includes the asynchronous route preparation fix.

With the user's confirmed live microphone, resetting the device changed the
native microphone flag to off after 1.26 seconds and restored readiness after
4.56 seconds without restarting capture. This checks observed stream lifecycle,
not acoustic quality or sample-level stop latency. Manual Disconnect stayed idle
for over 13 seconds; Retry now reconnected. Native recovery controls and states
were read from the accessibility tree. Closing the main window recorded zero
visible main windows while the core continued reconnecting. Holding the board in
ROM mode for approximately one minute exercised absence and the complete
1/2/4/8/15-second capped backoff, followed by automatic readiness with the window
still closed. This did not simulate a physical power loss.

Turning Mac Bluetooth off produced BluetoothOff and paused scanning. Turning it
on restored readiness in the same native process with the microphone off, while
the window stayed closed. Bluetooth was left on and the previously connected
audio accessory reconnected. Explicit native Quit was observed to terminate the
application; the next native process could acquire the device. Explicit Quit was
also exercised while waiting for an absent device: all native processes exited, the board was restored to normal operation, and a new native
process recovered readiness. No old retry continued after exit.

Host coverage adds fresh-observation-only handle resolution, delayed device
appearance, recoverable scan failure, capped retry deadlines/reset on readiness,
old completion rejection, disconnect/heartbeat ordering, target switch, manual
cancel/retry/rescan and terminal-error retry suppression. Existing atomic audio
gate tests remain mandatory. A pending-route test cancels before worker
acknowledgement and confirms that a late acknowledgement cannot reopen output.
Route waiting runs off the connection state lock. Swift checks verify retry JSON
decoding, shared
native labels/control availability, weather failure and model shutdown without
starting a radio. These host checks do not replace actual permission dialogs or
a second physical device.

The source archive has no Git metadata. The complete repository gate ran in a
disposable source copy through `tools/source-gate.py`; no Git repository was
initialized in the working source. Product host checks and strict checks ran on
the actual product source. The final app retains its Bundle ID, minimum macOS 13,
static C ABI and BLE v2 messages. Native executable SHA-256:
`d8f7da83989bd768e05a6a826f5a167c9c9b33f92ea63c7f61b2e2fe1facf311`. The existing verified firmware archive and the
custom device partition table/game storage were not modified or flashed.

## Passport 0.3.0 companion and fishing — 2026-10-15

| Check | Current result |
| --- | --- |
| Build | PASS: final ESP-IDF 5.5.3 firmware, verified merged image and matching debug archive |
| Host tests | PASS: 3 C test groups, 45 actual LVGL states, 24 Rust tests and Swift native checks |
| Repository gate | PASS: complete gate in a disposable source copy; repeated for the final source and this record |
| Device tests | PASS only for authorized app-only flash, write-hash verification and a 15-second startup observation; interactive acceptance remains pending |
| Unverified | Physical game timing/keys, animation and Chinese rendering, NVS power-cycle retention, active audio/heap/stack regression |

Five apps now appear in a three-row scrolling list: Pomodoro, Microphone, Pixel
Companion, Mini Fishing and Settings. Companion actions are Feed, Play and Outfit;
focus completion and catches reward growth/treats. Fishing has cast/wait/bite/
catch/miss states and a six-species discovery guide. Both work without a Mac and
stop their visible animation when leaving. Fish casts are canceled on exit,
pairing or timer completion; completed collection rewards stay intact.

The pure model tests cover menu wrap/window mapping, long-release suppression,
feeding/free-play/outfit unlocking, saturated counters, reward exactly once,
focus versus rest, offline/background timer interaction, early/deadline/late
catches, monotonic wrap, exit cancellation and all six species under a fixed
seed. Save tests cover 32-byte roundtrip, all 256 single-bit corruptions,
truncation/invalid values with unchanged output, allocation failure cleanup,
unknown/corrupt NVS preservation, successful asynchronous acknowledgement,
commit failure/retry, safe enqueue gating, coalescing and immediate catch-result
checkpoints with write throttling. The final save adjustment was rechecked with
all three C groups after the full product host run; Rust/Swift were unchanged.

The production screen/font/art sources are rendered with the same 24 KB LVGL
pool and RGB565 partial-buffer configuration used by the prior preview. All 45
states pass active-font glyph, label-bound and framebuffer art checks. Two batched
visual inspection passes covered the new list, pet reactions/outfits, pond bite
feedback and guide. `build-host/ui-preview/games.png` contains illustrative host
renders, not physical-device images or the user's progress. The heading font now
contains 153 glyphs and 4,694 bitmap bytes. Original pixel scenes are authored in
`main/screen_play.c`; no full-screen bitmap or extra page timer is allocated.

Verified archive: `build/firmware/3027ad2ed529449479d19ceadbff988781957e848116d56159110b413d2d2cbf/`.
Merged SHA-256: `3027ad2ed529449479d19ceadbff988781957e848116d56159110b413d2d2cbf`.
Matching ELF SHA-256: `652e4ad29f8a1ae94d3dd6866bbfcab2ad96c81f5a9cc8afc08c9386c726fb8e`.
Final application: 1,747,776 bytes, 10,080 bytes larger than 0.2.1. The matching
archive MAP reports static D/IRAM usage 168,518 bytes (+144); the storage worker
also allocates a 3,072-byte stack, task metadata and a one-entry save queue at
runtime. Static size is not a runtime free-heap measurement. Audio-load heap,
largest block and stack margins remain hardware checks.

Page IDs 5/6 extend telemetry while 0–4, capabilities and audio messages remain
unchanged. C/Rust vectors include both new pages; the existing Mac core treats
page as an opaque byte, so no companion binary update is required. Saves use only
`passport/play_v1`, do not migrate desktop settings and never clear Bluetooth
bonds. The bundled partition binary matches the prior 0.2.0 archive. The later authorized
flash found a different custom layout on the actual device, as recorded below;
only its factory application was updated.

Authorized flash on 2026-10-15: the user approved the exact 0.3.0 application
update preserving settings/bonds/progress. The known ESP32-C3 v1.1, 8 MB target was
confirmed at `<Passport USB serial port>`. Initial partition-byte verification failed;
reading only its partition metadata revealed a valid custom table: NVS 0x9000/
0x6000, PHY 0xf000/0x1000, factory 0x10000/0x340000, plus cardid, save_restore,
wifi_config, pokewalk_store and recovery partitions. All eight entries were checked
for bounds/non-overlap, and the 1,747,776-byte application fits the 3,407,872-byte
factory capacity. The named recovery subtype warning from the generic partition
parser was not a layout failure and no partition was changed.

The verified application alone was written at 0x10000 (sector erase range
0x10000–0x1bafff); the write hash passed. Bootloader, partition table, NVS/PHY and
all extra data/recovery partitions were left intact. No whole-chip erase or
original-firmware backup was performed. The bundled merged image represents the
minimal build layout and must not be used as a data-preserving refresh of this
custom device. Startup logs confirmed the preserved custom partition table,
factory boot, version 0.3.0 and ELF prefix `652e4ad29`, plus display/buttons/audio/
battery initialization and BLE advertising. No crash or progress-store warning
was observed in the bounded 15-second window. Idle telemetry: internal free heap
52,824 bytes, largest block 43,008, main/audio/BLE TX stack high-water values
4,256/24,292/1,976. These are idle checks only; rendering, game controls, real NVS
save/reboot retention and live audio still require interactive acceptance. The
serial port was closed after observation.

Pending device checklist:

1. Visit all five entries and verify scroll/wrap, selection memory and long-OK
   return without a trailing click. Inspect glyphs, contrast and animation.
2. Feed the initial pet, play and inspect the locked outfit hint. Catch fish
   offline, check early/late misses and guide discovery without losing treats.
3. Complete a one-minute focus phase while fishing/offline: cancel the cast,
   show the completion alert, grant one 20-XP/two-treat reward, then verify a rest
   phase grants no reward. Verify scarf/crown progression with earned XP.
4. Wait for the save indicator to clear, power-cycle, then verify growth,
   treats/outfit and collection survive, while the timer/game return to idle.
5. Repeat microphone/BlackHole speech regression, stop on exit/disconnect and
   watch heap/stack margins with the new storage worker alive. Exercise a
   background focus completion during microphone use and save after capture stops.

## Passport 0.2.1 visual refinement — 2026-10-15

| Check | Current result |
| --- | --- |
| Build | PASS: ESP-IDF 5.5.3 product firmware and independently verified archive |
| Host tests | PASS: C controller/vectors, 20 actual LVGL rendered states, 24 Rust tests and Swift native checks |
| Repository gate | PASS: complete gate on disposable source copy; static checks repeated after this record |
| Device tests | NOT RUN for 0.2.1; the earlier 0.2.0 flash/startup record below remains separate |
| Unverified | Physical color/contrast, glyphs, frame timing, all keys, live audio/RSSI and heap under transmission |

The chosen C concept is retained under `assets/images/passport-pixel-concept.png`.
The firmware now uses a thick authored clock face, shaded pixel sprites, Bluetooth
and battery icons, RSSI-driven bars and an amber stepped full-row focus frame.
A small licensed 20 px heading subset complements the existing dynamic CJK font.
Pairing, reset/unpair confirmation and timer completion have dedicated panels;
long city names cannot hide the leading stale-weather marker. There is no new
background animation or full-screen raster allocation. Protocol and Mac binaries
are unchanged.

The host renderer compiles the real `main/screen.c`, the pinned LVGL 9.5.0 and both
production font assets. It uses RGB565, a 40-row partial buffer and a 24 KB built-in
pool, checks visible glyphs/label bounds, and checks actual framebuffer colors for
the clock, sprites and frames. Primitive fills are clipped to the current partial
draw area before task creation; the pixel checks catch the previously observed
text-only output with missing art. The 20 states include all apps/selections,
120-minute countdown, confirmations, pairing, completion, weather variants,
negative temperature, stale long city and unknown/offline data. Generated PPMs and
`home-apps.png` under `build-host/ui-preview/` are illustrative host renders with
the BSP-equivalent corner mask, not device photographs or live readings.

Verified archive: `build/firmware/97ff91f70f4809b0030731bd3e5e5824363bf84423951adc31a7c2b78bd3a250/`.
Merged SHA-256: `97ff91f70f4809b0030731bd3e5e5824363bf84423951adc31a7c2b78bd3a250`.
Matching ELF SHA-256: `841728535c19c6b4db6cc8c8c5669a9501b5639a1abdac4022d23136cfd81ac4`.
App size: 1,737,696 bytes, 9,984 bytes more than 0.2.0; partition layout unchanged.
The first build invocation used a different Python executable alias than the
existing cache; rerunning with the matching Python 3 entry point passed without
cleaning or changing configuration. Read-only handoff discovery currently finds
no USB Passport port; no new flash, reset or erase was performed.

## Passport 0.2.0 — 2026-10-15

| Check | Current result |
| --- | --- |
| Build | PASS: native arm64 SwiftUI app and ESP-IDF 5.5.3 multi-app firmware |
| Host tests | PASS: product C controller/shared vectors; 24 Rust tests; Swift native checks; strict Clippy |
| Repository gate | PASS: static checks, root host tests and baseline firmware build on a disposable source copy |
| Device tests | PASS for authorized segmented flashing, write-hash verification and a 15-second startup observation only; interactive acceptance remains pending |
| Local audio regression | PASS: new diagnostics BlackHole loopback, 48 kHz stereo F32, 995.3 Hz, final 500 ms silence peak 0.000000 |

Firmware archive: `build/firmware/f2214f26ac95b7133516832cb30d022a9847acf9f7affadcd1ca23c92df9cba9/`.
Merged SHA-256: `f2214f26ac95b7133516832cb30d022a9847acf9f7affadcd1ca23c92df9cba9`.
Matching ELF SHA-256: `3eaa7948862b57b18520a76a6cc2f1218307abf2c32c0396eb3a49b0832aa133`.
The application is 1,727,712 bytes in the unchanged 8 MB NVS/PHY/factory layout.
The archive was independently checked by the existing firmware verification tool.
The root gate builds the baseline demo; never flash that image as this product.

Native app: `desktop/target/release/Passport.app`, local ad-hoc signature, deployment
target macOS 13. Rust/Opus use a separate `target/macos13` cache and the C compiler
flags confirm the 13.0 deployment target. Native executable SHA-256:
`c163dcef11f7c42de6c1102bff9481ae2bde68452ba51f66203241bf5b6e735d`.
This machine runs a newer macOS; running on macOS 13 itself is not verified.
The Command Line Tools linker reports absent optional Developer search paths;
these are warnings, not missing linked dependencies or ignored test failures.

C coverage includes app navigation, long-release suppression, timer background/
pause/reset/completion, immutable running durations, monotonic wrap, stale microphone
ACKs, stopping during a completion alert, time/weather bounds and capabilities.
Shared C/Rust vectors include new capabilities, settings, time, Chinese weather,
timer and stop/ack messages. Rust also checks migration without touching the old
configuration, command admission, deduplication, parser/sequence bounds, Opus,
resampling/backpressure and a canceled setup that cannot reopen output permission.
Swift checks city query escaping/decoding, weather bounds, HTTP failure and a
preview lifecycle that does not start Bluetooth.

Live native screenshots checked the Device, Microphone and Preferences pages in
explicit preview mode, without establishing another BLE connection. A long route
explanation was changed to wrap. Window-close/menu-bar persistence, notification
permission and full quit cleanup still require interactive acceptance; the later
computer-control close-window attempt timed out. New device screens are authored
in LVGL with pixel numerals/icons, independent from the baseline demo shell.
Source glyph inventory covers every new firmware Chinese character; physical
rendering remains unverified.

On 2026-10-15 the user authorized flashing after connecting the device. USB discovery
and chip identification confirmed one ESP32-C3 with 8 MB Flash at
`<Passport USB serial port>`. The old and new partition binaries were identical. The verified
archive components were written at 0x0 (bootloader), 0x8000 (partition table), and
0x10000 (application); all three write hashes passed. NVS and PHY ranges were not
written, and no full-chip erase was performed. A bounded 15-second serial window
confirmed version 0.2.0 and the matching ELF prefix, display/buttons/audio/battery
initialization and BLE advertising, with no observed crash. Idle telemetry reported
56,840 bytes free internal heap, largest block 47,104 bytes, and main/audio/BLE TX
stack high-water values 4,256/24,292/1,948. This is idle evidence, not audio-load
acceptance. The serial port was closed after observation. Do not publish private
settings or audio.

Remaining hardware acceptance: first pair/reconnect/cancel, real RSSI, Chinese screen
layout and keys, offline timer/weather, sleep/resume, notification behavior, memory
and audio-stack load, stopping on exit/disconnect, and speech through the rebuilt
core. The user confirmed the previous version's speech path worked; that is a
regression baseline, not a PASS for this new firmware.

Reproduce with `python3 tools/validate.py --host`, `bash tools/build-macos.sh`,
`python3 tools/validate.py --firmware` in activated IDF 5.5.3, and
`python3 tools/source-gate.py --mode=--all`. The original source remains without
Git metadata; no commit, push, publication, driver change or bond reset was made
by this refactor. The supplied duplicate root handoff was preserved under
`docs/development/mac-source-handoff.zh_CN.md` with its English pair and fixed links.

<details>
<summary>Historical controller evidence (before 0.2.0)</summary>

Automated evidence and hardware acceptance are separate. The current Windows
implementation has passed the C controller/shared-vector test and 18 Rust tests:
request deduplication, sequence wrap, stream parser splits/resync/bounds, names,
link allowlist, bounded audio/drift/flush, 1 kHz resampling, real 16 kbps Opus
roundtrip/PLC, route backpressure with a closed output gate, all 6,874 embedded Chinese glyphs, and rendering three desktop pages
with 16 entries at the minimum window size. Repair regressions cover all four CCCD
transitions, PIN validation/deduplication, pairing cancellation, authentication
before GATT and retaining the native link throughout the protocol session. These do not validate BLE,
physical buttons, real audio or the device screen. Final build/gate results and
artifact identities are recorded alongside the delivered build report.

## macOS development checkpoint — 2026-10-15

Native Apple Silicon development now uses Rust 1.99.0, Apple Clang 21 and
CMake 4.4.4. The desktop imports the connection state machine: UI admission
blocks duplicate scans/connections, attempt epochs reject stale requests,
pairing and protected-service setup accept cancellation, and disconnect cleanup
is bounded to five seconds. A missing saved device requires an explicit rescan.
New attempts clear old errors; disconnected startup no longer claims pairing
has failed. Fatal worker errors require restarting the app.

The three existing pages now use explicit dark-theme selection, larger controls
and text, progress states, empty-list guidance and an unavailable-output warning.
Audio diagnostics show desktop decoded peak level, received/decoded packet counts,
local queue drops and decoder errors. Counters describe the most recent stream,
refresh on the BLE heartbeat, and contain no speech samples. They do not prove
that BlackHole or Codex receives audio.

The arm64 release `.app` builds and launches successfully; `cargo clippy
--locked --all-targets -- -D warnings` and `cargo fmt --check` **PASS**.
Executable SHA-256: `0bdb8b0e7f3d7bd32c345a715edfcc5ca80fab735dd8ae3bb389dad24c368d42`.
Live screenshots confirmed the connection/audio pages and dark theme; session-page
rendering remains covered by automated tests. Native product C controller/shared-vector tests and **23 Rust tests PASS**.
The latter include the four now-integrated connection-model tests and packet
backpressure diagnostics. The locked Opus dependency declares CMake 3.1 policies;
Mac build/validation entry points supply `CMAKE_POLICY_VERSION_MINIMUM=3.5` for
CMake 4 without editing downloaded sources. Set the same environment variable
when invoking Cargo directly in a fresh build directory.

The complete repository gate was attempted on a disposable source copy and
**FAILS** on a pre-existing extra root `HANDOFF.zh_CN.md`: it has no English
pair and its product-relative links do not resolve from the root. The source
snapshot remains without Git metadata. Firmware rebuilding is **NOT RUN**:
ESP-IDF 5.5.3 is unavailable. The included historical firmware archive still
passes hash/layout verification; this is not a new firmware build.

BlackHole 2ch 0.7.1 is installed. After the Mac reboot, both its input and output
were enumerated. Explicit BlackHole-to-BlackHole loopback **PASS**: 48 kHz,
two channels, F32, approximately 995.3 Hz test tone and final 500 ms silence peak
0.000000. The bridge saved output is BlackHole 2ch. This test used only the
virtual input and saved no recording. Read-only serial discovery previously
found no Passport USB candidate. The GUI showed a completed protected BLE handshake and an idle connected
state across observations, with the microphone off: **PASS for this limited
scope**. First-pair dialogs, cancellation/reconnect, sleep/resume and prolonged
load have not been accepted on hardware. Passport speech and Codex transcription remain **NOT RUN**. No firmware, bindings or system default audio settings were changed; the user
explicitly authorized the BlackHole driver installation. The original speech failure remains
unresolved pending this end-to-end acceptance.

## Reproduce

Run `python tools/validate.py --host` with CMake, a native C compiler and Rust.
Windows can also run `tools/test-host.ps1` with C++ Build Tools. Run
`python tools/validate.py --firmware` from activated ESP-IDF 5.5.3. The product
does not reuse an ignored root sdkconfig. Desktop builds use Cargo.lock;
macOS needs its own native test/build and the generated permission-bearing `.app`.

Run the unchanged root gate separately in all three forms: static, firmware,
complete. A source archive without Git metadata can run
`python tools/source-gate.py --bash <bash-path>`; it copies source under root
`build/validation-source-*`, initializes only that copy, and keeps results.
Set `CC` and `ACTIONLINT_BIN` to native compatible tools on Windows, activate
IDF first. The wrapper makes native `python3`/`idf.py` available to Git Bash and
removes the MSYS identifier only inside native IDF Python. The original source
directory must remain without `.git`. Temporary Git metadata is for validation,
not a commit, remote or publication.

## Acceptance matrix

Record each result independently on Windows and macOS. Windows VB-CABLE local
loopback is **PASS** (48 kHz, two channels, F32, approximately 995.3 Hz tone,
final 500 ms peak 0). Authorized COM3 flashing/write verification and startup are **PASS**. The user
confirmed the Chinese screen and long-OK settings entry/return on the initial image.
On 2026-10-14, Windows first authenticated pairing, saved-bond reconnect, encrypted
NUS handshake and a 30-second bidirectional idle heartbeat check passed with the
repair. Other board checks and all macOS checks remain **NOT RUN**. These limited
checks do not complete any full acceptance row below.

The subsequent Realtek USB test passed discovery at approximately -66 dBm,
authenticated pairing, approximately 45 seconds of GUI-backed idle heartbeats
and an isolated saved-bond encrypted handshake plus 30-second bidirectional
heartbeat check. The user selected Realtek enabled/onboard Intel disabled.
Speech and long-duration acceptance remain pending; an audio task historical
stack margin of 452 bytes after GUI use requires load review before sign-off.

| Item | Automated evidence | On-device acceptance method |
| --- | --- | --- |
| Keys/state | C state tests: cyclic/empty/single list, repeated pending navigation, preparing cancellation, stale ACK, long-click suppression, recording block, timeout/reconnect | UP/DOWN with 0, 1, 3 and 16 sessions; click/long OK rapidly; prove a held OK never starts capture on release |
| Protocol | Shared C/Rust vectors, every split, Rust malformed/coalesced/bounded parsing and sequence/dedup tests | Negotiate MTU 23 and larger; send corrupt length/version, duplicate request, stale stream; verify bounded heap and fail-closed partial transmission |
| Audio routing | Windows VB-CABLE short tone/silence loopback PASS; real Opus 960-sample/120-byte roundtrip + PLC; resampling bound and flush | Repeat local loopback on macOS; route Passport speech over BLE, verify rate/channels, silence/underrun and no buffered replay after mic stop |
| Chinese UI | Source cmap verified; egui checks all advertised glyphs; strict UTF-8/name validation | Inspect every device view, long name, empty/error/selected states; verify Chinese glyphs and rounded-corner/clipping clearance |
| BLE pairing | CCCD and pairing lifecycle regressions; Windows first authenticated pairing, saved-bond encrypted handshake and 30-second idle heartbeats PASS (limited) | On each OS: first pair/code, bond reconnect, cancel pairing, Bluetooth off, sleep/resume, permissions denied, explicit device + OS bond reset |
| Codex control | Canonical URL allowlist and OS launch error handling | Configure three real local sessions; press each direction and check opened thread; test one-item list, deleted/invalid thread and missing protocol handler |
| End-to-end mic | Route-before-capture handshake and atomic output gate in implementation | Manually open Codex voice input, select virtual mic; speak into Passport, verify input; stop and confirm silence, disconnect/reconnect keeps mic off |
| Stability | Bounded buffers, no recording accumulation; drift loop exercised in host tests | 100 mic toggles, 20 reconnects, 30 min continuous capture; measure heap, largest block, audio/BLE/main stack margins and inspect sanitized exception logs |

## Fault and data notes

The current desktop showed intermittent discovery after the short secure-link
PASS, with approximately -98 to -104 dBm RSSI. The user confirmed its external
antenna is absent. Sustained GUI operation remains unverified until the RF
setup is corrected and retested; driver problem code 0 and access Allowed alone
do not validate radio performance. See the current [build report](BUILD_REPORT.md).

The Realtek USB follow-up corrected discovery for the tested configuration and
passed the short encrypted connection/reconnect checks above. Windows rejected
two simultaneously enabled radios with BTHUSB event 6; keep only the selected
adapter active. Close the GUI before running `ble-check` so the diagnostic has
one connection owner. Range and sustained audio are separate acceptance checks.

Missing virtual output fails start and keeps capture off. Disable the route during
capture and check the desktop error plus device stop. Exit the bridge during
pairing and recording; confirm prompt cancellation and silence. Test fast
start/stop/start and verify old queued packets never enter the new stream.
Long settings entry stops mic; list/audio-route edits also stop mic before syncing.
Resetting pairing deliberately removes stored bonds. Firmware does not erase NVS
to recover initialization errors. Flashing a merged image can replace stored
settings; approve the chosen image and data impact before flashing. No physical
flash or OS virtual-driver installation is implied by software validation.

## Resource acceptance

Firmware uses no PSRAM: LVGL 24 KiB pool + partial draw buffer, codec 24 KiB
stack, bounded BLE queues, static PCM, and once-allocated encoder compete with
NimBLE and BSP/DMA for internal RAM. Review the ELF/MAP sizes and measure minimum
heap/largest block after secure pairing while encoding. On-device encoding must
complete each 60 ms frame consistently; if it cannot, report failure rather than
calling dropped audio a successful end-to-end test. Virtual-device callback
latency and independent clocks require a real loopback/drift test.

</details>
