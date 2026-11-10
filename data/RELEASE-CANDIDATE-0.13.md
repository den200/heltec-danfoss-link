# CC replacement — release candidate 0.13, 22 October 2026

## Current deployment: 0.13.1

CC OFF, seven paired IDs enabled and saved, AUTOSTART ON (generation 76).
Firmware 0.13.1 changes room/device metadata only. Active spare V3 state recovered
at 09:06:12 and ACKed its 17 C reply at 09:08:06. Six heads have ACKed takeover
replies. Saved seven-head EN reset at09:17:05 recovered exact ownership and
targets; six responding heads ACKed again by09:22:06. Node5 remains unheard;
user deferred batteries/placement. Temporary guard stopped after this check.
No physical power-cut or whole-house acceptance pass is claimed. Room names/areas are verified in HA; Small bedroom / 5
is inferred pending a physical wake. Existing targets are unchanged.
See BRIDGE-PROGRESS.md for the bounded guard and remaining acceptance gates.

## Confirmed physical result

0.12 with CC off, node16 only: MQTT17→19 C applied on the head. User then
changed it locally to22 C; it stayed there with no alarms. Capture has5 replies
and5 matching ACKs. Remote context1→2 and local2→3. Saved SHADOW verified
08:42:15/08:42:36 before user restored CC. This is a short functional test, not
an hour trial. Evidence:bridge012-active-session.json/bridge012-active-report.json.

## 0.13 changes and verification

- V3 records bind each issued context to its target separately from the last
  link-ACKed target. Application transmission requires a completed write/readback
  of that exact pair. Old or concurrent checkpoint completion cannot authorize
  a different pair. Flash writes stay outside the radio task, after the immediate
  link ACK. A deferred reply can resume after the checkpoint while the head is
  still listening. Remote requests prepare/checkpoint before the next wake.
- Opt-in automatic recovery restores the exact issued pair. A crash before TX
  and after TX/lost ACK therefore resend the same ID/target. Unprepared requests
  are discarded. V1/V2 lack the pair and migrate passive; no automatic takeover.
- Seven independent climate command routes/discovery:2,5,6,8,12,13,16. MQTT cannot
  enable ownership. Deleted15 excluded. Existing per-head serial CONTROL and
  AUTOSTART explicitly set ownership. Climate availability requires ownership,
  recent reports and a durable pair. Other heads remain disabled by default.
- Room estimate, setpoint and low-battery entities retained. Short polls cannot
  freshen an old battery sample. Unknown/unheard values remain unknown.
- Authenticated node5 actually sent22-byte polls (six observed, RSSI about-103
  to-109dBm), previously classified Other and ignored. Exact poll shape now
  receives the normal reply. This does NOT prove node5's temperature is known
  or its RF coverage is adequate.
- All six host suites pass with ASan/UBSan: durable intent, manual state,
  persistence, multi-head MQTT parsing, telemetry and protocol.415 genuine CC
  replies reproduce exactly;416 MAC frames pass. PlatformIO build passes.
- 0.13 flashed/digest verified, boot08:52:09 in SHADOW, all heads disabled,
  AUTOSTART OFF. Wi-Fi/MQTT connected. Pre0.13 NVS backup and0.12 rollback retained.

## Required commissioning evidence

1. With CC off, enable only16; checkpoint AUTOSTART ON, reset Heltec, verify
   recovered ownership/target and resumed ACKs. Verify local updates survive
   their checkpoint. Capture write duration and deferred reply behavior.
2. Bring up remaining paired heads at their existing targets; checkpoint
   ownership. Observe per-head replies/ACKs and HA state. Resolve weak node5 RF
   rather than asserting coverage. Reboot with saved ownership and verify.
3. Keep CC off only after controller/coverage checks pass. Otherwise saved
   SHADOW (and verified disable) before restoring CC.

ID255 currently fails closed rather than guessing overflow. This is an explicit
release limitation: indefinite manual-update operation needs a verified wrap
rule. A codec test or link ACK alone cannot establish that rule. There is no
claim that whole-house replacement is already accepted.
