# First local-control test — 2026-10-21, Europe/Copenhagen

User requested 33 C, then explicitly selected 28 C after checking Danfoss's specified
4–28 C range. Target: spare head in room A, UI lc-013, observed radio node 16.
Before test user confirmed the head displayed 19 C.

## Key persistence

`data/network-key.json` contains Home ID, verified key and algorithm, mode 0600.
A mode-0600 copy is on Pi at
`~/danfoss-captures/20261020-234401/network-key.json`.

## Two bounded one-shot transmissions

A temporary Heltec build adds an explicit serial command limited to the known Home ID,
source 1, destination 16, short setpoint message and 28 C; maximum three attempts per
boot, no boot-time or automatic transmission. Only TWO attempts were issued.
0 dBm, 868.40 MHz, 40 kbps FSK, inverted payload with inverted 55 F0 delimiter.
The T-Embed remained an independent receiver and captured both exact test frames.

1. 00:01:04.966: firmware status 0; radio sequence 5, application byte 0f, mode 00.
   `ffd2bb2d0141051b10910002100a0f0d000000000af02cbd511da6`
2. 00:02:08: radio sequence 4, application byte 08, mode 02, based on a fresh CC
   reply at 00:01:48. Another genuine report/reply occurred at 00:02:04 before this
   queued transmission; thus this attempt was already behind the latest exchange.
   `ffd2bb2d0141041b10910002100a080d000000020af0daa251a4f2`

No acknowledgement from node 16 was recovered for either test transmission.
User subsequently reported that the head still displayed 19 C. **Control not achieved.**
The head did send valid status reports at 00:01:48 and 00:02:04; the genuine CC replied
with extended configuration containing 19 C (`076c`), and the head ACKed those replies.

## Interpretation and next experiment

These attempts establish on-air emission and independently decodable/authenticated
frames, not acceptance by the head. Neither was synchronized to the short report/reply
exchange. Timing, application sequence, duplicate handling, mode/message variant,
physical-layer tolerances and active CC behavior remain possible causes. Authentication
key verification is independent and still valid; no evidence here invalidates it.

Next test should implement an on-device timed responder, record ACK and subsequent
application behavior, and coordinate the genuine CC so it does not race or overwrite
the test. Do not repeatedly transmit stale frames or claim RF ACK proves setpoint application.

## Firmware restoration

Original actual flash contents backed up from 0x10000 for 0x100000 bytes, both on Pi
and in `data/heltec-original-app.bin`. Test image and hashes are preserved in
`firmware-test/` and `data/firmware-test-manifest.json`. Original firmware restored
following the attempts; consult manifest for completed restoration status.
