# Controlled pairing — preliminary findings

User reports one head added in room A, automatically labelled lc-013, connected on
both CC and head. Display showed 17 C; user subsequently increased the setpoint.
User subsequently confirmed the change was 17 -> 19 C on the CC. Confirmation
received around 23:52 CEST; exact action time not supplied. At the 23:52:15 check,
no 19 C command to node 16 had yet been recovered.

## Timeline (Europe/Copenhagen)

- 23:46:44–50: existing node 15 receives repeated `90 03 22c9dd0983` and probes.
  It later replies/ACKs (23:47:13 and 23:47:36). Thus this command and unanswered
  probes alone do NOT establish that the addressed device is dead or was removed.
  The previous node-3 removal interpretation remains consistent with user context,
  not a proven opcode definition.
- 23:48:26: node 16 starts captured range/configuration exchange. Candidate association
  with room A / lc-013 follows the user's report and timing; UI name is not a radio ID.
- CC sends identical `80 04` 16-byte value seen previously. Head responds `80 05 00`.
- 23:48:27–28: further configuration and bidirectional replies for node 16.
- 23:49:52, 23:49:58: extended CC config includes `06a4` (1700).
- 23:50:12: short CC setpoint command body `0e0d0000000006a4904fe4b3`
  contains 17.00 C in the previously identified setpoint position.

## New evidence about trailing tags

These whole application payloads, INCLUDING their presumed trailing tags, repeat
under different node addresses:

- node 14 -> CC and node 16 -> CC initial report, ending `ffff5bf8467c`.
- CC -> node 15 and CC -> node 16 config, ending `0028009fcca694`.
- node 15 -> CC and node 16 -> CC reply, `9100021005090d0030e77645`.

This supports testing deterministic application-level authentication/integrity first.
It is evidence against simple direct binding of these tags to radio node addresses or
an always-changing session nonce. It does not rule out reset/reused counters, different
rules for different message types, or compensating state, and does not prove the key.

## Capture limitations

Snapshot preserves original database rowids and raw JSON strings. Recording continues
on Pi until 00:44. Only the R1 window acknowledgement has been observed so far, not an R1
packet. Full Assign IDs exchange is not recovered. Unknown-home checksum candidates
in noisy windows must not be treated as genuine traffic solely because XOR matches.
