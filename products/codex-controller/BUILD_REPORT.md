<p align="right"><a href="BUILD_REPORT.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Build and delivery report

Validation date: 2026-10-14. Product version: 0.1.0, BLE repair checkpoint.
The source directory remains without `.git`; repository gates use disposable
copies with temporary Git metadata. No commit, push or publication was performed.

## Separate results

| Field | Result | Scope |
| --- | --- | --- |
| Build | PASS | ESP-IDF 5.5.3 standalone ESP32-C3 firmware; Windows x64 release executable; merged layout and matching ELF/MAP verification |
| Host tests | PASS | Product C/CTest including all four CCCD transitions; 18 Rust tests, formatting and Clippy with warnings denied; repository static, firmware and complete gates |
| Device tests | PASS (limited BLE scope) | Approved COM3 flash/write verification and startup; Realtek USB BLE discovery, authenticated pairing, approximately 45 seconds of GUI-backed idle heartbeats, saved-bond encrypted handshake and a separate 30-second bidirectional heartbeat diagnostic. The full acceptance matrix remains incomplete |
| Unverified | Remaining | Full button/UI/protocol matrix, MTU and fault injection; speech routing and real Codex control/voice input; cancellation, Bluetooth off, sleep/resume, permission refusal; 100 toggles/20 reconnects/30-minute capture; macOS native build and hardware checks |

The user confirmed the Chinese control screen and long-OK settings entry/return
on the initial 2026-10-13 image. Windows VB-CABLE local loopback also passed then:
48 kHz, two channels, F32, approximately 995.3 Hz from a 1 kHz tone, final 500 ms
peak zero. These earlier checks were not repeated for this BLE-only repair and
do not establish the Passport speech-to-Codex path. macOS remains **NOT RUN**.

## Repair and evidence

Windows access was **Allowed**, with Bluetooth powered on. The firmware treated
Windows' CCCD initialization `0 -> 0` as a lost subscription and terminated the
link during pairing. It now stops only on `1 -> 0`; the four transitions have a
host regression test. Encryption, authentication, Secure Connections and bonding
remain required.

The bridge now scans without the backend's advertisement UUID filter and checks
the NUS UUID in the application. Windows uses custom `ProvidePin` pairing with
authenticated encryption and a bridge-owned six-digit dialog. A native
`GattSession` maintains the physical link for both first pairing and bonded
reconnects, and stays alive throughout the protocol session. Releasing it after
backend connection had reproduced a later write timeout; session ownership now
has a regression test. Pairing cancellation removes handlers, completes deferrals,
cancels the OS operation and clears the input. Errors retain their stage and
underlying details; write timeouts identify message type and fragment.

The user confirmed successful first pairing with executable `6f6485a7...`.
The later `a3b7a61b...` checkpoint still timed out after connecting and is
superseded. The final executable below passed a saved-bond encrypted handshake
and 30-second bidirectional heartbeat diagnostic. Device logs reported
`encrypted=1 authenticated=1 bonded=1`. The diagnostic deliberately disconnected
afterward; this cleanup is not an unexpected disconnect. The broader stability
matrix remains pending. No passkeys, bond keys, personal links or audio were logged.

Subsequent GUI validation was not completed: discovery became intermittent and
one post-reboot service discovery returned no RX characteristic. Scan RSSI was
approximately -98 to -104 dBm. Read-only PC checks found the Intel Bluetooth
adapter enabled, problem code 0, services running, access Allowed and no related
error/warning events in the preceding 12 hours. The user confirmed that the
ASUS ROG STRIX X570-I GAMING desktop has no external antenna attached. Its
[manufacturer manual](https://dlcdnets.asus.com/pub/ASUS/mb/SocketAM4/ROG_STRIX_X570-I_GAMING/E15891_ROG_STRIX_X570-I_GAMING_UM_V2_WEB.pdf)
requires installing the bundled antenna at the rear wireless ports. Missing
antenna is a supported RF hypothesis, not proof that the PC or Passport radio
is defective. Restore the antenna or use a separate BLE adapter, then repeat
discovery and sustained GUI/reconnect acceptance. The earlier short PASS must
not be interpreted as a stable end-to-end connection. No driver or system
Bluetooth settings were changed during these checks.

### Realtek USB adapter follow-up

On 2026-10-14 the user connected a Realtek Bluetooth 5.0 USB adapter (driver
1.9.1038.3002). It initially showed problem code 31 while the onboard Intel
adapter was active. Windows BTHUSB event 6 explicitly reported that only one
active Bluetooth radio was supported. With the user's approval, the Intel
adapter was disabled and Realtek restarted; Realtek then reported problem
code 0. Windows confirmed BLE and central-role support.

Realtek discovered the Passport at approximately -66 dBm, compared with the
earlier onboard results of approximately -98 to -104 dBm. Device logs confirmed
authenticated encrypted pairing and approximately 45 seconds of idle heartbeats
from the GUI connection. One overlapping GUI/CLI diagnostic returned an
authentication error and is not a valid isolated reconnect result. After the
user closed all GUI instances, the final executable independently passed
discovery, saved-bond encrypted NUS handshake and 30 seconds of bidirectional
heartbeats with the microphone off. It disconnected deliberately afterward and
the serial observer released COM3. No firmware, driver or bond was removed.

The user explicitly chose to keep Realtek enabled and onboard Intel disabled
after testing. This supersedes the earlier discovery blocker for the tested USB
configuration; it does not prove that the onboard radio is defective. Range,
speech-to-Codex input and the complete stability matrix remain unverified.
After GUI use, the audio task's reported historical stack margin was 452 bytes;
the isolated idle diagnostic did not run capture. Review capture stack use under
load before audio/stability sign-off rather than treating BLE PASS as audio PASS.

The Windows flow follows Microsoft's [custom pairing guidance](https://learn.microsoft.com/en-us/windows/uwp/devices-sensors/pair-devices),
[desktop API restrictions](https://learn.microsoft.com/en-us/windows/apps/desktop/modernize/desktop-to-uwp-supported-api)
and [MaintainConnection contract](https://learn.microsoft.com/en-us/uwp/api/windows.devices.bluetooth.genericattributeprofile.gattsession.maintainconnection?view=winrt-26100).

## Delivered artifacts

Paths are relative to this product directory. Generated files remain ignored.
Packages contain paired usage/protocol/acceptance documents and dependency notices.
Initial ZIPs are retained under `build/initial-delivery/` for historical evidence;
they are not the current deliverables.

| Artifact | SHA-256 |
| --- | --- |
| `build/codex-controller-firmware.zip` | `46c1656a7101663fb860039cfd55a0eeaa637ebae32ec43716b515ed3f06fd11` |
| `build/desktop-windows.zip` | `843113bc8dba9e61686819cf39ce0f6ced1f026301d20fe8f5d9ac414ec3d7fe` |
| `build/desktop-windows/passport-codex-bridge.exe` | `7bc79b6adb56f5f3a24ebf7517fa1d8894725d27e23e1fcce33a85a78c876a51` |
| Merged firmware at 0x0 | `1f15a1fdd07d76de043dbc037e460e2673b86bc4d50e3338a19ca09f98bf45e9` |
| Matching firmware ELF | `730d61dc2b413ac776028ee41467f4d45435118c22cface4feff35a207d6aaf1` |
| Matching firmware MAP | `c242434ec68123d2506b362970757c3d344b8c432fe400b6985eae055bdf6878` |

The verified firmware archive is `build/firmware/1f15a1fdd07d76de043dbc037e460e2673b86bc4d50e3338a19ca09f98bf45e9/`.
Its manifest binds the application descriptor, retained ELF/MAP, component
images, merged bytes, offsets and 8 MB Flash capacity. The desktop manifest
records executable/document/notice hashes. The firmware ZIP retains this archive
under `firmware/`, including debugging artifacts and component images.

## Toolchain, layout and runtime scope

ESP-IDF v5.5.3, RISC-V GCC esp-14.2.0_20251107, LVGL 9.5.0 and fixed-point
`78/esp-opus` 1.0.5; ESP32-C3, no PSRAM, DIO 80 MHz, 8 MB Flash. NVS remains
0x9000/0x6000, PHY 0xf000/0x1000, factory app 0x10000/0x7f0000.
The application is 1,722,256 bytes within its 8,323,072-byte partition; the merged
image is 1,787,792 bytes. Chinese font coverage remains 6,874 glyphs, 16 px, 2 bpp.

On 2026-10-14 the user approved this exact image on COM3 with NVS/bond reset.
esptool 4.12.0 confirmed ESP32-C3 v1.1 and 8 MB Flash and verified the write hash.
The image was written at 0x0 through 0x001b478f, with sector erase through
0x001b4fff, without a whole-chip erase. Startup ELF identity matched `730d61dc2b41...`.
No additional firmware flash was needed for the final desktop connection fixes.

During the final idle heartbeat check, sampled free heap was 55,808–55,836 bytes,
largest block 47,104 bytes. Reported main/audio/BLE TX stack margins were
4,404 / at least 24,260 / 1,308 bytes. No panic/reset appeared in that bounded
capture. These are idle, paired measurements; capture/encoding load and the
60 ms encoding deadline remain unverified. Serial observation was closed and
COM3 released afterward.

Windows used Rust 1.96.0, MSVC C++ Build Tools and CMake 3.30.2. The executable
needs the x64 Microsoft Visual C++ runtime. No virtual-driver installer is bundled.
Windows uses VB-CABLE; macOS uses open-source BlackHole. The lightweight bridge
supplies Passport BLE/session control and Opus routing; no custom driver is needed.

## Remaining acceptance

Follow the [eight-item matrix](VALIDATION.md#acceptance-matrix). For bonded Windows
diagnostics, close the GUI and run `passport-codex-bridge ble-check <scan-identifier>`;
first pairing uses the GUI. No configuration, Codex link or audio capture is
changed by this check. Local driver verification uses
`passport-codex-bridge loopback "CABLE Input" "CABLE Output"`.
Real conversation navigation must be checked in Codex; the device shows the
configured target, without readback of the current Codex page. Start voice input
manually and select the virtual microphone before end-to-end speech acceptance.
