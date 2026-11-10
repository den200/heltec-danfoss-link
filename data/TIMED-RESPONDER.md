# Timed responder preparation — 2026-10-21

Target: Home ID ffd2bb2d, spare head node 16 (room A / lc-013).
Prepared temperature: 28 C, using an extended CC-like response with both temperature
fields set to 28 C and mode/revision 03 from the latest genuine CC response.

## Operation

Firmware: `firmware-test/`. On boot it runs the normal listening-post receiver.
`DANFOSS_OBSERVE` enters passive report detection, explicitly disarmed.
Hardware sync = inverted Home ID + node-16 source (00 2d 44 d2 ef); receive the remaining
52 bytes of the observed 57-byte report. Verify exact type/length/source/destination,
RF checksum and XTEA MAC before treating a report as valid.

`DANFOSS_ARM` enables ONE response burst, expiring after 120 seconds. A valid report
consumes the arm before transmission. The burst contains a radio ACK echoing the report's
radio sequence, then a 28 C extended reply echoing its application sequence. All computation
and transmission happen on the ESP32, without SSH/SQLite/USB command-roundtrip delays.
Afterward reception continues disarmed. A failure does not automatically retransmit.
`DANFOSS_DISARM` cancels arming. Generic commands such as STOP also disarm and exit
responder mode. No arming command was sent during preparation.

The T-Embed remains the independent receiver, including for ACK evidence. An RF ACK
alone does not establish application-level acceptance; head display/report must confirm.

## Tests completed

- PlatformIO firmware build successful.
- Embedded C++ MAC matches all 113 saved validation frames.
- Actual node-16 report passes authentication and shape validation.
- Wrong source, altered authenticated body with repaired outer checksum, and short
  frames are rejected.
- Generated ACK and extended 28 C reply independently checked in Python, including
  authentication, RF checksums, target and application-sequence echo.
- Hardware timing and actual acceptance remain experimental until observed on air.

## Human steps, one at a time

1. Leave CC powered. Change spare head from 19 to 20 C with its own buttons and
   report when done. This is observe-only, to confirm the new receive path and trace
   genuine request/reply behavior.
2. After successful observation, user temporarily unplugs CC power and reports it off.
3. Agent explicitly arms a 120-second one-burst response, confirms arming, then user
   briefly presses head button to trigger communication. Do not reset or remove pairing.
4. Check independent RF capture plus user display. Reconnect CC afterward.

## Recovery

Original actual app backup: `~/danfoss-captures-tools/heltec-original-app.bin`
and `data/heltec-original-app.bin`, flash offset 0x10000, length 0x100000.
Use FLASHING lock, stop only Heltec logger, write backup with esptool, restart logger.
Original capture session remains active until 00:44 and will send STOP/disarm then;
it restores the paused wide/mt timers but does not flash the original firmware.
Thus a passive timed-responder build may remain installed until explicitly restored.

## 00:09 observation

User changed head 19 -> 20 C. Genuine CC short replies at 00:09:39 and 00:09:48
contain 07d0 (2000) and mode/revision byte 03. Receiver validated normal 57-byte
reports at 00:09:33 and :48. The button-change report at :39 is shorter (53 bytes);
current responder intentionally does not trigger on that format. Wake/normal 57-byte
report is needed for this first timed test. Rejected false sync matches remain inert.
Updated response byte from 02 to observed 03; its meaning is not established.

## First armed-window result and correction (00:17)

ARM at 00:13:40 expired at 00:15:40 without transmission. Valid 57-byte reports
arrived at 00:15:47, seven seconds late. The user changed to 21 C; 53-byte reports
at 00:16:16–17 contained 0834 and were rejected solely by the supported-length guard.
User observed flashing antenna/bell icons while genuine CC was powered down.
No danfoss_response event was recorded: no timed ACK or command was transmitted.

Firmware revised to authenticate either 53- or 57-byte node-16 report, checking the
actual declared length/checksum/tag and ignoring trailing receiver-window bytes.
The 53-byte case waits four additional bytes (0.8 ms nominal at 40 kbps) before
RX completes; actual on-air timing still needs validation. Arm duration extended to
600 seconds, still ONE response burst maximum. Tests include the actual 21 C report,
its generated sequence-matched reply, and rejection after an authenticated-byte change.
Earlier two-minute/57-byte-only operation descriptions above are historical.
