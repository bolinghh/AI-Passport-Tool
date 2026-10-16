<p align="right"><a href="PROTOCOL.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Passport multi-app protocol v2

Authenticated/encrypted NUS is retained. UUIDs and the bounded v1 framing envelope
remain unchanged: `a5 5a`, envelope version `1`, type u8, payload length u16,
request u32, stream u32, sequence u16. All multi-byte values are big-endian.
Control writes use 20-byte fragments; payload max is 2048, Opus max 256 bytes.
The application version is negotiated separately; framing version 1 is not an
advertisement of compatibility with the previous conversation application.

| ID | Message | Payload and behavior |
| --- | --- | --- |
| 1 | HELLO | Empty request/echo; microphone stays off, not ready yet |
| 2–4 | Retired | Conversation list/select/ack; never reused; device rejects and disconnects |
| 5 | MIC | One byte 0/1, nonzero request; start requires stream=request |
| 6 | MIC_ACK | status u8 (0 success), requested value u8; matching request/stream only |
| 7 | AUDIO | Existing 16 kHz mono Opus 60 ms frames, 960 samples, bounded sequence/queue |
| 8 | HEARTBEAT | Empty, every 3 s; silence timeout 7 s |
| 9 | ERROR | Close audio; no automatic restart |
| 10 | CAPABILITIES | Exactly `[2, 15]`: app version 2, microphone/time/weather/settings capabilities; echo required |
| 11 | TIME | UTC epoch seconds u32 (2020+), signed UTC offset i32 (±86400); device advances with monotonic time |
| 12 | WEATHER | observation epoch u32, temperature i16 in tenths Celsius (-1000…700), WMO code u16, city UTF-8 byte length u8, city bytes (1…48) |
| 13 | SETTINGS_UPDATE | focus minutes u8, rest minutes u8, each 1…120; nonzero request |
| 14 | DEVICE_STATUS | battery i8 (-1 unknown), RSSI i8 (127 unknown), page u8 (home/apps/pomodoro/mic/settings = 0…4; pixel companion=5, fishing=6), mic state u8 (off/preparing/live = 0…2) |
| 15 | TIMER_STATUS | phase u8 (focus=0/rest=1), state u8 (idle/running/paused/done = 0…3), remaining seconds u32 (rounded up), completion counter u32 |
| 16 | SETTINGS_ACK | status u8 (0 success), matching settings request |
| 17 | STOP | Empty, nonzero desktop control request; immediately clear device capture/pending stream |
| 18 | STOP_ACK | Empty, matching STOP request; allows accepting a subsequent fresh start |

## Desktop C ABI recovery state (0.2.1)

The exported C functions and BLE message numbers/payloads are unchanged. JSON
commands add `{"type":"retry"}` to retry the current target immediately. Existing
`disconnect` cancels automatic recovery; `scan` cancels the old intent and scans
for selection; `connect` selects a target and enables recovery. Commands remain
available after recoverable transport errors. No new persistent settings exist.

`connection` snapshots retain `phase` and `target` and add `retry_enabled` (bool),
`retry_count` (u32, reset on readiness), and `next_retry_at` (Unix seconds or null).
New phases are `Waiting` and `BluetoothOff`; `Connecting` includes fresh discovery
before a physical connection, followed by `Syncing` until settings acknowledgement.
Scheduling uses monotonic time; the Unix deadline is for presentation only.
Manual cancellation and target switches invalidate the connection generation
before queued work can resume audio. A fresh parser, notification subscription,
request ledger and stream state belong to each physical session. Old callbacks
cannot mark the new generation ready or start audio. Recovery sends capabilities,
settings, time and cached weather again; it never sends a microphone start.

## Readiness and failure

HELLO must be followed by capability negotiation within 7 seconds. The desktop
becomes ready only after capabilities and settings acknowledgement. Previous firmware
will not answer capabilities and receives a bounded upgrade error. Previous desktop
sends a retired list; new firmware displays an upgrade hint and disconnects.
The standalone `ble-check` negotiates capabilities but never configures or captures audio.

Device status/timer telemetry is emitted each second. RSSI is sampled outside the
LVGL lock every five seconds; it is not inferred from presence or drawn as fixed bars.
Weather older than one hour is stale. Invalid updates do not partially modify state.
Time/offset are synchronized each minute and on Mac clock/timezone/wake changes;
offline timezone transitions require a later Mac synchronization.

Desktop stop/quit/disconnect immediately closes the shared atomic output gate. STOP
also waits for an acknowledgement before a new microphone start can be accepted;
queued pre-stop starts cannot reopen the route. A missing STOP acknowledgement causes
disconnection after 3 seconds. Leaving the device microphone page closes capture
locally before sending stop. Stream IDs, request deduplication, bounded parser,
sequence handling and fail-closed audio behavior remain in use. No speech is persisted.

Pomodoro uses elapsed monotonic milliseconds, independent from wall-clock sync and
BLE. Background navigation/disconnection does not reset it. Setting updates affect
idle/next phases only. Completion is latched once and never automatically starts
the next phase. Reboot resets the local timer and completion counter.

The 0.3.0 offline apps add only page IDs 5 and 6 to device telemetry. The envelope,
capability bytes and existing page IDs remain unchanged; the current 0.2.x Mac core
already accepts the page byte as opaque status. No game RPC, progress upload or
new desktop permissions are added. Local rewards/saves are independent of BLE.

## Swift/Rust boundary

`desktop/macos/Sources/CPassport/include/passport.h` is the ownership contract.
Create/destroy one handle; send tagged JSON commands asynchronously and poll JSON
snapshots. Free every returned UTF-8 string once. Do not destroy a handle while a
call is outstanding. Swift owns UI, time, weather HTTP and native notifications;
Rust exclusively owns BLE, settings validation/persistence, Opus and audio output.
Closing a window keeps the one core alive; quitting transfers destruction off the
main UI thread and closes audio first. Audio input permission is only needed for the
explicit BlackHole loopback diagnostic. No default microphone capture is performed.
