# Local control demonstrated — 2026-10-21

With the real CC powered off, the timed Heltec responder sent a radio ACK followed
by an authenticated extended 28 C command to head node 16 at 00:19:13 Copenhagen time.
Firmware reported ack_status=0 and response_status=0. User observed antenna/bell stop
flashing and setpoint 28 C, without manually selecting 28 C. This is a successful
one-command application-level control test, not a complete replacement controller.

Transmitted command:
`ffd2bb2d01410a251091000210140b0d000000030af00af002580af000000af06062887f06`

The XTEA MAC and RF checksum were independently verified after capture. Processing_us=933
measures firmware processing before sendFrame; it is NOT the entire RF response latency.
The independent receiver captured our radio ACK, but did not recover a head ACK to the
extended response in the inspected decode. The user's display observation establishes
application-level behavior; do not invent a missing RF acknowledgement.

The single response consumed the arm. Subsequent reports at 00:19:52 and 00:20:32 were
received without replies. The user reported the icons flashing again but setpoint staying
28 C. Loss of ongoing controller responses is consistent with this; exact icon timing
and firmware behavior are not independently established.

At 00:21:02, the revised receiver authenticated three repetitions of the user's local
27 C report: declared frame length 53, vendor length 0x24, body includes 0a8c (=2700),
application byte 16, tag 747e4044. The four bytes beyond the declared frame in the raw
receiver record are trailing noise and are excluded from tag/checksum validation.
This also confirms the shorter button-change report path now works.

No further command was sent to overwrite the user's 27 C setting. Responder remains
installed in passive/disarmed mode. Raw capture continues until its session deadline;
session STOP exits responder mode but does not flash back the original firmware.
Original firmware backup remains available for restoration.

Next work for a useful local controller: sustained authenticated request/reply handling,
radio/application ACK and retry behavior, per-head state, interpreting local-button
changes, schedule/override fields, controller restart behavior and MQTT integration.
Do not keep forcing 28 C in a repeated test loop.
