# Heltec replacement firmware — progress and acceptance gates

Updated: 22 October 2026, Europe/Copenhagen.

Latest: 0.13.1 is controlling the seven paired IDs with CC OFF after the user
deferred node 5 coverage. Saved AUTOSTART ON, generation 76. Six heads (2, 6, 8, 12, 13,
16) have matching reply ACKs; node 5 remains unheard. Deleted node 15 is disabled.
Room names and areas are verified in the authenticated Home Assistant device list.
See room-map.json and ha-room-mapping-20261022.png. Older entries are historical.

## Current deployment — 22 October, 09:22

- 0.12 physically passed MQTT 19 C and local 22 C without alarms on spare 16.
- 0.13 adds V3 write-ahead recovery and seven-head MQTT routing. Six sanitizer
  suites and the PlatformIO build pass. 0.13.1 adds room/device metadata only;
  radio/control logic is unchanged. Binary flash digest verified.
- Saved active spare state recovered at 09:06:12 after firmware restart;
  node 16 ACKed its recovered 17 C reply at 09:08:06. This is a reset recovery
  check, not a physical power-cut test of V3.
- Other paired heads enabled at their existing targets at 09:09:23. At 09:13:13,
  ACK counts were 2:1, 5:0, 6:1, 8:1, 12:1, 13:1, 16:2. Storage clean,
  zero save failures/queue drops, Wi-Fi and MQTT connected.
- All 28 HA discovery records retain entity IDs and now group four entities per
  named head: climate/setpoint, room-temperature estimate, low-battery signal.
  HA areas and all seven named devices verified visually after user login.
- Living room: 2 and 6 (radio mean 19.22 C matches app 19.2 C); Bedroom 3: 8;
  Master bedroom: 12; Guest room: 13; A: 16. Small bedroom: 5 is
  provisional by elimination, with no fresh room sample. Office has no mapping.
- Small bedroom wake: user sees 16 C, low battery and connection/bell alarms.
  No packet captured after that wake. Requested fresh batteries and more central
  placement if practical. Node 5 coverage is the immediate gate; identity remains
  inferred. Do not claim whole-house completion until it responds.
- User explicitly deferred Small bedroom batteries/placement and requested that
  work proceed with the other six. Recovery guard replaced the initial window.
- Seven-head saved-state EN reset at 09:17:05 recovered all expected enabled IDs
  and unchanged targets, excluding deleted 15. Fresh 09:22:06 status shows one
  post-restart matching ACK for each of 2, 6, 8, 12, 13, 16; node 5 remains silent.
  Storage generation 76 is clean; save failures and queue drops zero; MQTT up.
- Temporary guard explicitly stopped after this six-head recovery check.
  Controller remains active, AUTOSTART ON, CC OFF. Node 5 stays configured for
  control but is not communicating. Do not restore CC while bridge is active.
  This is a six-head deployment with a known coverage exception, not proof of
  whole-house operation. No further CC switching is requested.
- ID low-byte 255 remains fail-closed. Indefinite operation and V3 interrupted
  writes are unaccepted; no claim that firmware is finished.

Evidence: commissioning-013-session.json, commissioning-013-report.json,
commissioning-20261022-raw.jsonl, bridge-0.13.1-manifest.json, room-map.json,
room-discovery-live.json, ha-room-mapping-20261022.png,
commissioning-0131-recovery.json, commissioning-0131-mqtt.json.

## Current manuals — 22 October 2026

README.md now indexes docs/FIRMWARE.md, docs/HOME-ASSISTANT.md,
docs/OPERATIONS.md and docs/INDEX.md. These are source-audited0.13.1 manuals,
with explicit installed-versus-example HA configuration, current V3 intent
recovery, exact topics/state fields and ownership/rollback procedures. Historical
MQTT/persistence notes point to the current manuals; original evidence retained.
Documentation work does not change firmware, active ownership, broker or HA
configuration. Node5, ID255 and V3 power-interruption limits remain explicit.

## Reporting convention

Each work report should state: completed work, verification result, current hardware
mode, remaining gate, next action, and any physical action needed from the user.
A successful build or transmit is not completion of a hardware acceptance test.

## Previous deployment (historical)

The 0.12/earlier passive and recovery stages below are superseded by the current
commissioning status above. The room-A head is node 16; deleted spare 15 is excluded.
CC remains off during active commissioning. Historical captures and rollback
images are retained.

## Initial deployment (historical)

- Bridge 0.2 flash digest verified; boot event confirms radio=true and shadow=true.
- Previous bridge app backed up before writing; hashes and paths in
  `bridge-0.2-manifest.json`.
- `danfoss-bridge-capture.service` owns serial, accepts atomic command files and
  polls STATUS every 30 seconds. No CONTROL/SET commands queued.
- Independent T-Embed RF capture restarted with a fresh two-hour window; raw
  records verified. Previous receiver window had expired despite logger activity.
- Awaiting spare node 16 power and observation, followed by explicit CC-off
  confirmation before enabling control.

Post-flash check: authenticated node-8 report and CC reply received; target learned
as 18.5 C. Queued STATUS round-trip succeeded; every head disabled, zero bridge
replies. Evidence: `bridge-0.2-shadow-check.json`.

## Spare readiness checkpoint — 11:29

User powered the spare. A newly active authenticated node 15 appeared at
11:28:18; node 16 remains silent. Awaiting a labelled local 17 C button report
to identify the physical spare before selecting the control node. CC responses
to node 15 have not supplied a target; no CONTROL or SET command has been sent.
Evidence: `spare-readiness-20261021.json`. Next: identify spare, establish target,
then obtain CC-off confirmation and enable only that verified node.

## Spare identified — 11:30

User confirms possible head mix-up and agrees to use node 15. Its authenticated
53-byte local-button report at 11:29:44.566 carries 17 C, matching the labelled
21-to-17 C action. STATUS confirms targetKnown=true, target=1700; all heads remain
disabled. Awaiting explicit CC-off confirmation. The control test will use node 15,
not the originally assumed node 16.

## Control trial started; ACK fix — 11:35

User confirmed CC off. Bridge 0.2 answered node 15 at 17 C three times; user
reported no connection warning on spare, warnings on other heads as expected.
No head ACK was recorded, leaving the target pending; a successful transmit
is not delivery proof. Bridge 0.3 now adds a bounded 25 ms receive window at
exact ACK length (10 bytes total, 6 after Home-ID sync), then restores normal
report RX. C++ regression/build pass; flashed and digest verified. Capture
restarted and only node 15 re-enabled at 11:34:44. A remote 18 C target is queued
to test actual application on its next report. Awaiting ACK and displayed target.
The one-hour acceptance clock restarts after this firmware change.

## Remote command proven; fast-turnaround trial — 11:40

User confirmed the spare settled at 18 C after a minus-button wake-up, proving
the queued remote command applied with the CC off on bridge 0.3. Head ACK capture
remained unresolved, so bridge 0.4 enables RadioLib XOSC standby (documented in
the installed library as faster RX/TX switching). Built, flashed and digest verified.
Only node 15 was re-enabled at 18 C at 11:39:46.556. The one-hour clock restarts
with this build; local-button precedence still requires a separate check once
command delivery is acknowledged. Latest evidence: `control15-latest-report.json`.

Automatic thread follow-up `danfoss-spare-bench-check` checks every five minutes,
bounded to 15 runs, staying quiet unless there is a meaningful result or required
action. It must stop/deactivate after trial completion/abort, and must queue SHADOW
and verify bridge control disabled before asking the user to restore CC power.
If repeated replies still fail ACK capture, investigate/report and end an
unproductive trial instead of keeping the CC off indefinitely. The independent
T-Embed witness was temporarily retuned to sync 44d2, tag zw40reply15, for a
five-minute reply-capture diagnostic; restore its general receive window for
further RF checks.

## ACK reception confirmed — 11:41

The first bridge-0.4 reply at 11:39:56.285 has a matching node-15 link ACK.
STATUS reports reply_acks=1 and pending=false. XOSC fast turnaround resolved
the observed ACK capture failure. Remote command application was already visually
confirmed at 18 C. User is now asked to change locally 18 to 17 C and confirm it
stays there without a warning. Next gate: verify local-button adoption in STATUS
and on display, then continue the uninterrupted run until at least 12:40 local.
Automatic checkpoints remain active. The T-Embed is restored to a general
receive window, sync aa0f, tag zw40bench04, duration 7200 seconds.

## Local-button precedence passed — 11:43

User confirmed that the local 18-to-17 C change stays at 17 C without a connection
warning. STATUS at 11:43:35 reports target=1700, pending=false, six replies and
six matching head ACKs, with zero transmit API failures. Only node 15 remains
enabled and the CC remains off. Session metadata on both Pi and local workspace
records the visual confirmation so checkpoints need not request this test again.

Remaining gate: one uninterrupted hour from the first 0.4 reply at 11:39:56,
with regular reports/replies and no unexplained overrides. Earliest elapsed pass
is 12:39:56 (report as about 12:40 Copenhagen time). At the end, obtain final
display/warning confirmation, queue SHADOW and verify disabled control before
asking the user to restore CC. Automatic checkpoints remain active. After this
bench gate: persistence/reboot recovery, then Wi-Fi/MQTT/HA, then room migration.

## Parallel local development — 11:47

While the unchanged bridge-0.4 continues its on-air acceptance run, develop
bridge-0.5 persistence/recovery locally. DO NOT FLASH this work during the trial.
The verified running 0.4 image is preserved as `data/bridge-0.4-verified.bin`
(and on Pi). Local `.pio` build outputs will belong to 0.5 after the next build.
Target: versioned, CRC-checked A/B NVS records; persist last accepted targets and
explicit ownership settings; discard unacknowledged commands/sequences at boot.
Fresh installs remain SHADOW. Automatic resume requires explicit AUTOSTART ON.
SHADOW must durably disable automatic resume before the CC is restored.
This is a separate development milestone, not a reason to restart the running test.

## Parallel persistence implementation complete — local only

Bridge 0.5 now builds with a versioned/network-bound CRC32 codec, alternating NVS
slots and readback validation, accepted-target checkpoints, explicit AUTOSTART ON
and durable SHADOW ownership release. Pending commands/sequences are excluded.
Host fault/recovery tests pass under ASan/UBSan; 113-frame protocol regression and
three Python tests pass; PlatformIO build succeeds. Hardware recovery, interrupted
saves and RF timing during flash writes are NOT accepted yet. Full policy and
bench procedure: `data/PERSISTENCE.md`. Manifest: `bridge-0.5-manifest.json`.

DO NOT FLASH 0.5 during the active 0.4 hour trial. Current PlatformIO output is
0.5; the verified 0.4 binary remains in `data/bridge-0.4-verified.bin` and on Pi.
Next: finish current trial, then run the separate 0.5 recovery bench before MQTT/HA.

## Radio trial completed; SHADOW verified — 12:56

Radio gate passed after 76.64 minutes on unchanged bridge 0.4: 26 reports, 26
replies and 26 matching head ACKs; zero TX API errors, no reboot, max report gap
4.84 minutes. All 52 report/reply data frames independently pass Python MAC
verification, and both application/radio sequence wraps are observed. Local target
remains 17 C. Only node 15 was enabled throughout the trial.

SHADOW acknowledged at 12:56:34.305; STATUS at 12:56:34.410 confirms every head
disabled. User asked to restore CC and confirm final spare display/warning state.
Those physical confirmations are PENDING, not invented. Bench-check heartbeat
`danfoss-spare-bench-check` is PAUSED. The app stored the requested short cadence
as a 30-minute recurrence, so this trial ended later than the planned 12:40 cutoff.
Evidence: `data/control15-acceptance-20261021/RESULT.md` and final-radio-result.json.

Next: confirm restored CC, then run the separate 0.5 recovery/power-cycle bench.
0.5 is still LOCAL ONLY; no persistence build was flashed during the radio trial.
Wi-Fi/MQTT/HA and household migration remain subsequent milestones.

## CC restoration follow-up — about 14:18

User returned much later, saw the spare in warning after the test had ended, and
restored CC power; boot is underway. Final visual observation at trial shutdown
is unavailable, not a known failure during the 76-minute radio run. The bridge
remains SHADOW with every head disabled and zero bridge TX since 12:56. No CC
RF reply seen at the first 14:18 check; continue receive-only while it boots.
Next: verify genuine CC traffic/household recovery, then schedule the short 0.5
spare-only persistence/reboot bench. Do not reactivate bridge control while CC on.

## 0.5 installed; passive persistence passed — 14:46

User confirms household normal after CC restoration. Authenticated CC traffic
verified (41 replies observed after restoration before 0.5 installation).
Backed up actual partition/NVS/OTA metadata, verified 0.4 rollback image, then
flashed/digest-verified 0.5 while every head stayed disabled. NVS generation 2
checkpoint succeeds. Durable SHADOW persists through an ESP32 hardware reset;
node 12's 18 C target restores before any fresh CC reply, and all heads remain
disabled. Actual USB power was not disconnected; do not call this a power-cut test.

Current hardware: 0.5 SHADOW, CC on, no bridge control. User asked to unplug CC
again for a short node-15-only active recovery test. WAIT FOR CONFIRMATION before
enabling. The old hour-trial automation remains PAUSED; no second hour is needed.
Evidence: persistence05-passive-before.json, persistence05-passive-after.json and
bridge-0.5-manifest.json.

## Active recovery and pending-command exclusion passed — about 15:04

User confirmed CC off again. Only node 15 enabled at 17 C. ACK received at
15:02:21; accepted target checkpoint generation 7; AUTOSTART ON saved generation 8.
Queued 18 C verified undelivered/pending, with SAVE leaving the old accepted state
unchanged. Hardware reset restores only node 15 enabled at 17 C, pending=false,
AUTOSTART ON and generation 8. Other heads remain disabled.

Current hardware: 0.5 ACTIVE ONLY FOR NODE 15, CC OFF. User asked to change locally
17 to 18 C; wait for ACK/checkpoint before asking for physical Heltec power cycle.
Still need cold power-cycle and local-target persistence, followed by durable
SHADOW and passive reboot before CC restoration. Do not restore CC while active.

## Local target checkpoint passed — 15:17

User confirms spare stays at 18 C without warning. Authenticated local button
report at 15:14:38, head ACK and generation-9 checkpoint verified. Fresh STATUS
confirms target=1800, pending=false, storage clean, AUTOSTART ON and only node 15
enabled. Evidence: persistence05-local-before-powercycle.json.

Current hardware: CC OFF, 0.5 active for node 15 only. Await physical Heltec power
disconnect/reconnect, then verify restored 18 C and resumed ACKs. Finish with
durable SHADOW and passive reset before asking user to restore CC.

## Normal recovery bench passed — 15:22

User physically disconnected Heltec power for five seconds. Boot at 15:21:02
restored generation 9, local 18 C, pending=false, AUTOSTART ON and only node 15
enabled. A reply and matching head ACK followed at 15:21:14. Across the short
recovery bench: 9/9 replies with matching ACK, zero TX API errors; deliberate
resets are included, so this is not an uninterrupted-duration test.

SHADOW durably saved at 15:21:48. Hardware reset at 15:22 boots recovered=true,
shadow=true, AUTOSTART OFF, generation 10, all control disabled and no bridge TX.
Safe to restore CC; user confirmation pending. Evidence: persistence05-active-
session.json, persistence05-session-report.json, persistence05-powercycle-after.json
and persistence05-final-passive-after-reset.json. Ordinary reset/power recovery
passes; interrupted-save hardware and RF write stress remain unaccepted.

## CC powered on by user — 15:30 follow-up

User confirms CC power restored. At 15:30:09 Heltec remains passive: all heads
disabled, AUTOSTART OFF, clean generation 10, zero bridge replies since passive
boot. No authenticated CC reply observed yet at 15:30:19; allow CC boot and
head wake-up. Household warning clearance has not yet been confirmed.

## CC restoration complete — 15:53

User confirms household normal. Forty authenticated CC packets observed since
final passive boot; Heltec remains SHADOW, every head disabled, AUTOSTART OFF
and zero bridge transmissions. Normal recovery bench is closed successfully.
Evidence: persistence05-cc-restored.json. Remaining: interrupted-save hardware
and RF write stress, then Wi-Fi/MQTT/HA. No active spare test remains running.

## Real NVS fallback and diagnostics — 16:07

With CC on and bridge passive, a storage-only/no-radio bench passed 241 injected
corruption/truncation cases in separate dfbench namespace. Production generation
10 and passive ownership recovered after restoring verified 0.5. Electrical
interruption during an actual write remains untested. See NVS-BENCH.md.

Bridge 0.6 diagnostics now flashed/digest-verified in SHADOW, all heads disabled,
AUTOSTART OFF. Build, 113-frame protocol regression, sanitizer persistence suite
and three Python authentication tests pass. Timing/counter fields added; active
RF/write stress still pending. 0.5 rollback image retained on Pi.

User authorized MQTT broker installation on Home Assistant. Official Mosquitto
app installation underway via signed-in HA UI; no broker was installed/running.
HA local address is <ha-ip> (homeassistant.local); homeassistant resolves
through Tailscale on this Mac. Use LAN address for future Heltec connection.

## MQTT infrastructure ready; 0.7 local — 16:14 follow-up

Official Mosquitto 7.1.2 running on HA at <ha-ip>:1883; MQTT integration
configured successfully. User must create/save dedicated broker login and fill
private local network file before Heltec connection tests. Do not put passwords
in chat or capture command spool. See MQTT.md and saved screenshots.

0.7 adds bounded standalone Wi-Fi/MQTT, spare-only discovery/commands, retained
replay guards and explicit pending/accepted state. Also fixes a local-button
precedence bug: newer button changes cancel older pending commands. These changes
are LOCAL ONLY until credentials/build/deployment checks; hardware is still 0.6
SHADOW with CC on and all control disabled.

## MQTT private build ready; broker login Save pending

User filled private network config; header generated without printing credentials.
0.7 private build passes. Initial authenticated broker probe returned CONNACK 5:
the login had been added but Options Save was not applied before navigation.
User asked to re-add and complete separate Save. Do not flash/test credentials
until broker authentication succeeds. Runtime remains 0.6 SHADOW with CC on.

## 0.7 passive MQTT milestone passed — 17:10

Broker login saved and authentication passed. 0.7 flashed/digest-verified and
connected directly to Wi-Fi/MQTT. HA discovers Danfoss Heltec / Spare node 15;
unavailable is expected in SHADOW. Three live requests rejected, retained replay
ignored across forced MQTT session reconnect, synthetic HA birth republishes
configuration. Retained test command cleared. Five CC packets, no MCU reboot,
zero bridge TX and zero queue drops. All heads disabled, AUTOSTART OFF.

Next: explicit CC-off confirmation, then spare-15-only remote/local and outage
checks with RF timing diagnostics. Actual broker/Wi-Fi outage and interrupted
NVS-write hardware gates remain open. Screenshot: ha-heltec07-shadow.png.

## 0.7 active network/radio bench — about 17:36

User confirms CC off and only radiator heads (no wall RS sensors). Enabled only
node 15; AUTOSTART stays OFF. MQTT 17.5 C queued, extended reply ACKed at 17:23:47,
accepted checkpoint generation 12. User saw 18 after pressing up; this does not
independently confirm the prior 17.5 display or a local target change. Later 17 C
request delivered during another wake; pending-local test restarted with 16 C.
At 17:35:20 the new 16 C is pending, ready for explicit local 19 C check.

Mosquitto stopped at about 17:26; network status confirms MQTT offline. Node 15
reply and matching ACK at 17:28:37 prove radio contact while broker unavailable.
Broker restored and reconnect publishes the unchanged accepted 17.5 C. Eight
replies/ACKs so far, no MCU reboot or queue drops; NVS writes/readback 7.29–7.52 ms.
These writes occurred in quiet gaps, so simultaneous RF/write timing remains open.

Temperature investigation: routine words at offsets 40/42/44 fit two sensors and
compensated room estimate, backed by official two-sensor description and 561
capture events (median simple-compensation residual 0.02 C). Field mapping is not
reference-validated or published to HA. Node 5 still unheard, 16 stale overnight.
See HEAD-TEMPERATURES.md and head-temperature-candidates.json. A receive-only
T-Embed witness window was opened briefly; previous OOK reception restore queued.

## 0.7 trial closed; 0.8 safe handover fix installed — 17:45 follow-up

0.7 trial ended in durable SHADOW at 17:41:25 after 9 reports/replies and 9 matching
ACKs, zero TX API failures, no MCU reboot, maximum report gap 4.84 min. All 18 data
frames independently MAC-verified and 9 link ACK checksums verified. Broker outage
radio continuity and reconnect passed. User's 18 C view after pressing up does not
confirm the earlier 17.5 display; no explicit button target report was received.
Routine wake consumed pending 16 C before the coordinated 19 C test. That test was
cancelled, and its pending-local precedence gate remains unresolved.

Found/fixed stale RAM requests surviving ownership release: 0.8 SHADOW/CONTROL OFF
now discard pending targets, reply caches and queued MQTT requests, reverting to
last accepted target. Sanitizer tests pass; protocol's 113 fixtures and 3 Python
tests pass. Verified 0.7 rollback and current NVS backup retained. 0.8 flash digest
verified, recovered passive generation 15. On hardware with every head disabled,
SET 15 19 then SHADOW clears pending; SET 15 20 then CONTROL OFF also clears it.
Neither test transmitted. Evidence: bridge08-*.json and bridge-0.8-manifest.json.

Current hardware: 0.8 SHADOW, all heads disabled, AUTOSTART OFF. Broker running;
T-Embed restored to prior OOK receive mode and fresh OOK packets verified. User
asked to restore CC and confirm normal operation. Source/build/runtime now 0.8.
Next: restore CC, compare room temperature candidate with a reference, then expose
validated estimated room telemetry. Need a coordinated local-pending test and
actual Wi-Fi outage/RF-write stress later; no active control bench remains running.
Session reporting now includes queued MQTT targets and refuses an hour claim
across boots/control stops; replay checks retain 0.4's pass and reject interrupted
0.5's short recovery run. Console error lines are not counted as RF failures.

## Passive local report after trial — 17:47 follow-up

At 17:45:11 (after 0.7 trial closure), authenticated node-15 button report carries
15 C. 0.8 records it passively and saves generation 16 with AUTOSTART OFF; every
head remains disabled and zero bridge TX. This is not the cancelled active 19 C
precedence check. Spare temperature candidate rose with handling; a settled
reference remains necessary. HA UI now shows Unavailable as expected in SHADOW;
final screenshot ha-heltec08-shadow.png. CC restoration confirmation still pending.

## Lock / upper-limit failure identified — 17:53 follow-up

User reports flashing antenna/bell plus steady lock, could lower spare from 18 C
but not raise it. This is a failed local-control gate, not merely an alarm after
SHADOW. User confirms CC back on; fresh capture sees CC ACKs to household heads.
Latest 0.8 status 17:52:13: all disabled, no pending, AUTOSTART OFF, zero bridge TX.

Found unsafe extended builder assumption: offsets 20,22,26,30 all receive target.
Seven independently validated genuine CC extended frames in auth-validated-frames.txt
keep offsets 26 and 30 at 2800, offset 24 at 600; first two words vary (2100/1700
or 1700/1700). Actual bridge node15 TX at 17:23:47 used 1750 in all four fields,
then 1700 at 17:34:26 and 1600 at 17:39:17. Thus target changes also overwrite
configuration-like fields, a plausible cause of the ceiling/lock. Exact limits,
lock bits and first-two-word semantics still need mapping. Do not claim causality
or invent a field fix yet. The coordinated 19 C request was to the human; the later
passive SET 15 19 handover check never transmitted. This was not a delivered 19 C
command setting the ceiling. ACK acceptance never established config correctness.

Evidence: lock-investigation-raw.jsonl and lock-investigation-evidence.json.
Manual confirms flashing antenna+bell means no CC connection, independently of lock:
https://assets.danfoss.com/documents/latest/31020/AN07058640519301-010201.pdf (p16).
Next: confirm spare lock/ceiling clears under CC; capture genuine configuration
and replace extended-field overwriting with configuration-preserving behavior
before any further active test. Current runtime stays 0.8 SHADOW; no flash this run.
User says room estimates look correct; qualitative support, not precise calibration.

## CC reconnection does not clear spare restriction — 18:05 follow-up

User confirms antenna/bell alarms cleared with CC back on, but steady lock and
18 C upper ceiling remain when attempting to increase. Therefore returning to
SHADOW/CC connectivity is insufficient to restore unrestricted local operation.
Fresh 18:03:46 status: every head disabled/no pending, no bridge replies, auto OFF;
CC traffic observed including six proprietary packets addressed to spare 15.
Those six are short 22-byte messages, not 37-byte extended configuration restores.
Capture refreshed in lock-investigation-raw.jsonl. Requested user set spare room
20 C on CC and briefly wake head to capture genuine CC target/config behavior.
No new bridge commands or firmware flash. Configuration mapping/fix remains the
priority; do not proceed with broader control, local-precedence acceptance or
household takeover until restriction corrected and field handling verified.

## CC setpoint recovery attempt — 18:11 follow-up

User set spare's room to 20 C on CC; has not yet checked/woken the head.
Capture through 18:10:18 still has no delivered 20 C extended/short target reply
for node15. CC UI acceptance is not head application. Requested brief middle-button
wake and physical 20 C / local 21 C check. Bridge remains all disabled, no pending,
auto OFF, zero TX. No firmware change/flash while awaiting recovery evidence.

Expanded authenticated offline comparison: extended-configuration-audit.json.
Genuine CC examples include first two words 2100/1600 and 2100/1700, so these are
not universally duplicate fields. All genuine pre-bridge configuration examples
retain 600/2800/0/2800 in words at offsets 24/26/28/30. Bridge node15 extended TX
used 1700,1800,1750,1600 in offsets 26 and 30 at different times. A fix must preserve
configuration semantics rather than substitute another guessed full-frame pattern.

## Spare still limited; CC room association needs verification — 18:25 follow-up

User checked: spare still displays 18 C and lock blocks higher, while CC room
shows requested 20 C. That room's current-temperature view is --, unlike another
room (16 C target /16.7 C actual). User is removing/reinserting one battery;
physical restart result not yet reported. Advised no held button/factory reset.

Capture through 18:23:51: node15 sends routine temperatures (latest provisional
room field 21.16 C at18:22:53) and CC link ACK plus 22-byte proprietary responses.
No 27/37-byte target/config response to15 since CC restored. Other household nodes
receive 27-byte CC target commands. All active bridge tests remain stopped; zero
bridge TX, all disabled/no pending, auto OFF. This adds a CC association/state
question beyond the demonstrated unsafe extended builder. A 22-byte response's
application semantics are unresolved; do not label it a proven rejection code.
Asked exact CC room name and post-battery display/ceiling to establish correct
physical-to-CC mapping before further recovery action. No reset/re-pair instructed.
Evidence independently MAC-checked in cc-spare-recovery-evidence.json.

## Battery restart and firmware-key check — 18:32 follow-up

User reinserted battery: spare now displays21 C (previously18); CC room is "A",
shows21 C target but still no measured temperature. Lock freedom above21 remains
unverified. Captured restart18:24:41 includes31-byte startup and alternate reports;
CC still sends22-byte replies, not target/config. Current0.8 SHADOW all disabled,
no pending and zero bridge TX.21 C could be head startup behavior; do not attribute
it to CC/Heltec without delivery evidence. Asked local22 C and whether CC roomA
tracks it, to test both ceiling and physical/CC association. RoomA may be associated
with a different spare identity; this remains a hypothesis, not a mapping fact.

At user's suggestion inspected original CC update files offline. All manifest
sizes/MD5 and SB header SHA1 pass. Actual radio key fails SB AES header CBC-MAC
key-dictionary test; zero key also fails. No direct key reuse as wrapping key.
HEX checksums pass; Z-Wave3.52 string found, no current radio key in decoded HEX.
Main48.6MB application remains encrypted. See FIRMWARE-ANALYSIS.md and
firmware-key-audit.json. Originals unchanged; no device flash/control commands.

## Lock cleared; separate CC association evidence — 18:36 follow-up

User confirms local23 C now works without lock after battery restart. CC roomA
remains21 C/--, doesn't follow the head; CC reboot changes nothing. Turning CC off
causes spare antenna/bell alarm, restoring CC clears it. Thus transport contact
exists but application/room synchronization is not restored. Confirmed local23 C
MAC-valid button report18:29:10.577 from node15; latest18:34:54 status all control
disabled, no pending, no bridge replies. No test enabling or flash performed.

Historical data/TRANSMISSION-TEST.md identifies original roomA / UI lc-013 / node16;
today's physical head is node15. Earliest CC-to15 reply11:28:18.872 is already the
22-byte 0d01 form, BEFORE first bridge application TX11:31:19.697. This establishes
that this CC response pattern predates today's node15 bridge control. It does not
prove an exact removal/association cause or invalidate user's earlier working test.
The faulty extended-field builder is a separate demonstrated code issue.

Evidence: spare-association-timeline.json (MAC-checked). Asked user to inspect roomA
device names/IDs/count only, no reset/removal. Resolve actual CC association before
re-pairing or further configuration recovery. Top plan updated to current blockers.

## Correct spare restored; configuration-preserving source prepared — 19:40

User confirms head mix-up: removed batteries from deleted spare, powered the other
spare, and now CC/head synchronize setpoints both ways and show room temperature.
New authenticated node16 startup19:34:06, full CC configuration19:34:08 (21 C),
local20.5 C report19:34:24, CC responses20.5 then19.5 C confirm working exchanges.
User's known-good physical spare is16; deleted spare15 is off. No reset/re-pair needed.
Evidence: spare-swap-raw.jsonl and spare16-cc-recovery.json (26 MAC-verified events).
Latest hardware status19:37:12:0.8, all disabled/no pending, auto OFF, zero bridge TX.

Prepared0.9 surgery (NOT flashed): extended builder requires a per-head CC template,
changes primary target20..21 only, preserves opaque22..31 exactly. Learned only from
supported authenticated genuine extended CC frames while passive; no template means
serial/MQTT new target rejected. STATUS exposes configuration_known. RAM template is
not persisted: after reboot, new target writes require CC learning again; accepted
short-reply recovery remains as before. This is a deliberately limited candidate,
not proven replacement protocol semantics. Second word and mode semantics unresolved.

ASan/UBSan protocol113 fixture checks pass, plus genuine CC frame reproduction,
changed primary-target/unchanged config authentication and missing-template refusal.
Persistence and MQTT host sanitizer suites pass; PlatformIO heltec_v3 build passes.
No RF timing changes. Source backup:data/source-backup-20261021/bridge08/.
Private prepared binary/manifest:bridge-0.9-prepared.bin / prepared-manifest.json.
Runtime remains0.8 SHADOW; no bridge control command or flash issued this turn.
Next: retarget MQTT bench from deleted15 to usable16, deploy passive candidate and
learn genuine configuration; only then coordinate a short CC-off control/local-limit
check on16. Original authorization for node15 does not automatically enable16.

## 0.9 deployed in SHADOW; MQTT16 and passive telemetry — 19:49

User authorizes retargeting to correct spare16, passive learning then coordinated
short control test, and requests local temperature / low-battery HA advertising.
0.9 built and flash digest verified, boot19:46:57.816, recoveredgeneration26,
auto OFF, SHADOW/all heads disabled. Before-flash NVS backup and0.8 rollback retained.

MQTT subscribes only16/set; old15 topic rejected in host regression. Removed only
our old climate15 discovery; retired15 excluded from telemetry. Three read-only
entities each for2,5,6,8,12,13,16: room-temperature estimate, observed setpoint,
low-battery binary status. Climate16 also advertises room temperature beside target,
but remains unavailable until explicitly enabled; telemetry works with CC on.
FE00 is low, observed healthy status codes normal, unfamiliar status unknown;
rawword included, no invented percent. Only routine room field44..45 is published,
with independent15-minute expiry; button wakes cannot refresh an old room sample.
Measurements are not restored across boot; silent node5 unavailable. Exact CC
comparison still pending, labels explicitly say estimate.

Host ASan/UBSan protocol113 and config-preservation, persistence, MQTT and telemetry
replay tests pass; discovery JSON parsed independently. New telemetry tests include
fresh room, stale room despite button wake, low/healthy/unknown battery and missing
buffer handling. PlatformIO build passes. Expanded registry snapshot and JSON use
16KiB network-task stack; direct QoS0 state publishing from low-priority network task
every15seconds avoids a growing stale multi-head outbox. Radio timing unchanged.

Broker probe sees22 discovery entities. Real HA UI displays new sensors, including
node2 estimate20.0/target21.0 and node12 estimate18.1/target18.0; battery Normal.
Fresh passive result19:47:57: all control off, no pending, zero bridge TX. Live20 C
MQTT requestid1 to16 rejected in SHADOW at19:47:43 (receipt verified). No commands
to enable a head sent. Screenshot:ha-bridge09-passive.png. Private verified binary,
manifest, flashlog and bridge09-* evidence retained.

Asked user to set roomA20 C on CC, briefly wake spare16 and give CC current room
reading. Need a fresh genuine extended configuration this boot before new bridge
requests can be accepted. Configuration is currently RAM-only: standalone reboot
must eventually restore validated configuration, not just accepted targets; this
remains a firmware gate before household deployment. Next after passive learning:
explicit CC-off confirmation, node16-only primary-target/local-button/limit test,
then durable SHADOW before CC restoration. No extra hour automatically scheduled.
No custom HA app needed: normal MQTT entities, thermostat cards and automations can
own schedules. Firmware still owns RF timing, authenticated protocol/state/ownership;
head owns its internal temperature regulator. Wi-Fi outage/RF-write and node5
coverage gates also remain open.

Passive follow-up19:50: all six reporting usable heads now populate real HA room,
setpoint and Normal battery entities. Node5 remains unavailable, correctly. Spare16
shows estimate21.09 C and accepted19.5 C; exact CC reference requested. Configuration
still not learned (only short CC replies so far); no CC-off request or control
start issued. Snapshot evidence bridge09-passive-mqtt-live.json; screenshot
ha-bridge09-passive-readings.png. bridge09-passive-report.json generated with the
standard session reporter and a clearly labelled passive session, not acceptance.
Next physical action pending: CC roomA20 C + spare wake + measured-room reading.

## Passive configuration/reference gate passed —19:52

User confirms CC roomA20 C applied successfully and measured room21.1 C. Captured
node16 routine19:48:50 field44=21.09 C (HA21.1); this supports displayed precision
for one near-time reference, not an exact simultaneous calibration for all heads.
After handling, next routine19:51:46=21.34 C; do not hide time/thermal differences.

Genuine CC extended19:51:10.523 carries target2000, mode6, opaque configuration
07d002580af000000af0. MAC/checksum verified. STATUS19:51:28 confirms node16 target20,
configuration_known=true, every head disabled/no pending, auto OFF and zero bridge
replies. Wi-Fi/MQTT connected and no queue drops. Evidence:bridge09-cc-reference.json.

Asked user to unplug CC and confirm before starting a short node16-only control
and local-above-target check. No control command issued; waiting for physical
CC-off confirmation. Intended test uses whole-degree targets for unambiguous
visual confirmation, checks exact preserved tail in TX plus matching ACK, then
user local freedom/precedence. Successful RF ACK alone will not pass display test.

## Short0.9 node16 control test started —19:55

User explicitly confirms CC off and asks to minimize further distant CC trips.
Keep remaining physical checks in this one window; no repeated hour trial planned.
Only node16 enabled19:54:49.425 after fresh all-disabled / learned-config validation.
MQTT19 C id2 queued19:55:07.941, STATUS19:55:29 confirms pending1900; no delivery yet.
Every other head disabled; AUTOSTART remains OFF. User asked to middle-button wake,
verify remote19 C, then only if successful select local22 C and check no lock/alarm.
Those outcomes remain pending, not claimed. No additional physical CC cycle asked.

Finite Pi host guard PID206097 queues atomic SHADOW12minutes from session start,
by20:06:49 Copenhagen, unless trial ended earlier; then verifies all control off
from capture statuses. Guard uses command spool only, never serial directly.
This is a bounded failsafe for this specific test, not a recurring app automation.
Session:~/danfoss-captures/bridge-20261021/control-node16-session.json.
Local bridge09-active-{session,raw,report} evidence saved. Guard will stop active
control if user returns late; CC physical restoration still needs human action.
Next after received user checks: verify on-air preserved bytes and ACK/local report,
then durable SHADOW and confirm no pending before requesting CC restore.

19:55:42.281 first0.9 node16 extended reply primary1900, preserved opaque bytes
07d002580af000000af0. Independently MAC/checksum verified; matching node16 ACK
radioseq0 captured19:55:42.262 (TX event logged after ACK by design). This passes
radio delivery and exact-byte preservation only. Display19 / local22 / no-lock
physical checks still pending. Evidence bridge09-active-config-check.json.

##0.9 first-word-only physical failure; targeted0.10 correction —20:00

User reports head still20 C, connected/no alarms. Destination16 and matching
source16 ACK independently verified: no wrong-address mix-up. Primary1900 while
second word remained2000 was insufficient to change display. Mark0.9 application
test FAILED, despite valid link ACK. Hypothesis: second leading temperature word
is relevant to applied target, or another config/revision rule is involved.

Queued SHADOW19:58:56.243; verified all disabled/no pending, autoOFF19:59:30.
Closed0.9 trial with explicit physical failure. Standard report regenerated.
Prepared0.10: set two leading temperature words together, preserve24..31 exactly.
Sanitizer protocol113 fixtures and persistence pass. Builder reproduces recorded
node16 CC19 C extended frame exactly (mode2, radio15, app7), preserving600/2800/0/2800.
No RF timing or telemetry changes. Hardware effect still unproved.

Added narrow IMPORT_CC bench command: authenticates an exact supported37-byte
CC-to-bench16 frame with current key/home, checks target range and every head
passive/no pending, restores target/mode/config in RAM only; never enables control,
invents RX traffic, or changes key. Used to restore freshly captured19:51 CC frame
after test firmware reset with CC still off, avoiding another human CC cycle.
Not a substitute for production configuration persistence. Original deadline20:06:49
still bounds this window. Backup/verified0.9 retained;0.10 flash in progress.

0.10 flash digest verified, boot20:00:34.453 in SHADOW, recovered and autoOFF.
Authenticated genuine19:51 CC20 C configuration imported20:00:40.743 while all
heads passive. Import does not emit fake CC RX events or increment CC counters.
Only16 enabled20:00:54.706; new MQTT19 C id1 queued20:00:55.170 (receipt verified).
STATUS20:01:02 confirms only16enabled/pending1900/configknown, all others disabled.
Replacement host guard PID206675 retains ORIGINAL deadline20:06:49.409876,
not12minutes after the new boot. User asked to wake then verify19 C and local22 C
without lock/alarm. Physical outcome pending. No new CC on/off trip requested.
Evidence bridge010-active-*,bridge-0.10-manifest.json; private NVS backup and0.9
rollback retained. Current hardware active16only on0.10; do not assume0.9 SHADOW.

20:01:35.679 node16 ACKed0.10 extended reply with both leading words1900 and
unchanged tail02580af000000af0. Independent authentication/checksum validation
passes. This is delivery only; physical19/display22/no-lock checks still pending.
Evidence bridge010-active-config-check.json and refreshed standard session report.

## Trial stopped on deadline; late physical observation —21:08

User reports still20 C plus flashing antenna/alarm. Reply handled around21:07;
actual time the display was checked was not given. Guard had already queued SHADOW
20:06:49.757, saved20:06:49.880, verified all disabled20:07:05.462. Thus alarms
observed after that are expected with CC still off and cannot prove an in-run
connection failure. Application19 C / local22 C has NOT passed.0.9's prior20 C
observation was within its active run and remains a genuine primary-only failure.

Requeued SHADOW as immediate precaution21:07:22.251; STATUS21:07:22.811 confirms
all disabled/no pending, autoOFF, generation34. Asked user to restore CC and
confirm household normal. No additional CC-off cycle requested or planned now.
Corrected0.10 session end to actual20:06:49 guard stop (preserved the later erroneous
end-as-recheck snapshot remotely); do not report an hour of control. Standard
session report:2 replies/2 ACKs, no TX API errors/reboot,2.94min reply span, no local
button report,5.92min enable-to-stop. No visual acceptance. Guard result and latest
saved-SHADOW status copied locally; manifest/top deployment corrected to passive.

Offline lead: cc-target-mode-transitions.json. Node16 genuine manual changes
21→20.5→19.5→20 used raw mode3→4→5→6, while our failed candidates reused6.
But other captured target changes (scheduled/context unknown) keep the same mode:
10 of16 observed target transitions. Therefore mode is not proven a simple global
counter; investigate manual override/revision semantics and related43-byte
configuration exchanges before another source fix or live test. No guessed mode
increment flashed. Offline work needs no CC switching. Firmware remains unfinished;
HA telemetry/discovery success does not imply replacement-controller acceptance.

## CC restored; offline comparison only —21:12

User confirms CC powered back on and spare antenna/alarm no longer blinking.
Passive capture confirms genuine CC ACK/configuration traffic to16 and other
household nodes. STATUS21:11:16: all heads disabled/no pending, autoOFF, bridge
reply count remains2 on16 (the earlier trial only). Runtime0.10 remains unchanged;
no live control, firmware flash, new timer or physical action requested.
Evidence:cc-restored-after010-{raw.jsonl,json}.

Saved detailed authenticated CC/bridge exchange comparison in
SPARE16-COMMAND-ANALYSIS.md and spare16-command-comparison.json. New manual CC
requests advanced raw header word00000003→04→05→06, while bridge19 C reused06.
This is a concrete remaining difference; interpretation as revision/request
context is still a hypothesis, not a proven fix. Other apparent scheduled target
changes kept the word unchanged, so do not increment every reply. Related43-byte
configuration requests and22-byte responses also need exchange-level comparison.
No unsupported source changes made this pass. Firmware still not accepted for
household takeover; telemetry remains useful in SHADOW. Next work is offline
request/retransmission/context modelling and configuration persistence design.

## Offline investigation with narrow Claude checks;0.11 prepared —21:35

User explicitly requests continued investigation and authorizes specific Claude
questions with little context. Used claude -p in neutral/tmp cwd, no tools/MCP,
4 narrow prompts, no project/key/credentials supplied. Answers retained and
critically checked; no generated hypothesis was treated as proof.

Definite bug found: genuine CC context00000101 was truncated to modeByte01 and
retransmitted00000001. New source preserves full opaque4bytes. Context has
independent upper-byte changes as well as low-byte increments, so not a proven
single32bit counter. Local/button reply correlation2/2 and labelled CC actions2/2
support once-per-manual update ID; this is retrospective fit, not blind evidence.

Prepared0.11 (not flashed) also persists authenticated context/config in V2 A/B
records312bytes max. V1 migration is passive/no autostart because old records
lost bytes/templates. No guessed counter increment. Lossless codec and manual
policy separated; outgoing live-style policy remains0.10's unaccepted candidate.
408 unique genuine CC replies reproduce408/408 exactly, including5 upper-context
cases, unequal temperature words and operations10/50. Original113-frame suite,
sanitizer regressions and new V2 corruption/truncation/migration fallback tests
pass. Actual pre0.10 generation30/eight-head NVS fixture host migration passes.
PlatformIO build passes; private0.11 binary and manifest retained,0.10 source backup.
Physical V2 save/reboot/power-cut and application gates still open.

Follow-up correlation:32/33 observed0x50 replies followed by43byte config within2s,
0/4360x10 replies. The successful CC20 C on16 used0x10 with no43byte follow-up,
weakening missing-follow-up as universal cause. Flag meaning remains unproven.

Current live0.10 SHADOW reverified21:30:48:all disabled/no pending,autoOFF,
only2historical bridge replies; genuine CC traffic resumed. No flash/control this
turn. Asked one prospectively predicted passive check with CC ON: spare local22 C,
record starting display and CC roomA follow. Latest CC baseline1/17 C at21:27:56;
predicted next manual context2. No CC walk/on-off requested. See
INVESTIGATION-20261021.md for findings, Claude critiques, tests and uncertainty.


## Prospective passive manual-update check matched —22:30

User confirmed spare16 started17 C and selected22 C. The prediction context1→2
was recorded before the action. Authenticated button report22:28:29.883 carried
22 C; CC echoed22 C/context2 at22:28:29.960 (77 ms). Later routine replies kept2.
Extended CC reply22:28:57.408 contained22/22 C with unchanged tail24..31
02580af000000af0. This independently supports the candidate manual-update ID;
it does not establish bridge command application or byte-overflow/reset semantics.
No separate CC-screen follow confirmation was supplied.

Prepared0.11 codec reproduced7/7 new CC replies exactly; all8 new frames passed
MAC validation and host protocol ASan/UBSan checks. No firmware source/binary
change or flash. Fresh STATUS22:29:57:all disabled/no pending,AUTOSTART OFF,
node16 target22 C/context2, MQTT up, no queue drops; historical bridge replies
still2. CC stays ON. No physical action requested.

Next: implement/test a bounded manual-ID allocation model offline, including
retry identity, replacement commands after lost ACKs, local precedence and
recovery. Current0.11 intentionally has no manual-ID advance yet. After that,
actual application still needs one coordinated spare-only control check.
Evidence:passive22-check.json,manual-holdout-prediction.json,
passive22-context-correlation.json,passive22-replay-result.txt.


## Manual-ID candidate0.12 implemented and passively deployed —22:39

Changes are surgical to registry/reply selection, command guards and boot policy.
One low-byte ID is allocated when a new remote target's first reply is built;
unsent replacements do not consume IDs, repeated targets/retries do not advance,
and a replacement after a possibly delivered reply gets a different ID. Preserve
upper3bytes and configuration tail. Duplicate caches require the same target
revision. Local target changes cancel queued SET/cache and allocate once; byte255
is refused without carrying into flags. Only spare16 can be enabled.

Automatic recovery is intentionally not claimed for this new policy: without
write-ahead ID reservation, a lost ACK + crash could reuse an ID.0.12 always boots
passive, invalidates recovered context/config and refuses AUTOSTART ON. It retains
accepted targets and requires fresh real-CC data before enabling. Earlier0.5
recovery success does not prove recovery for this new manual-ID state model.

ASan/UBSan manual scenarios and deterministic interleavings from all256 starting
ID bytes pass, including lost ACK, replacement, local precedence, overflow and
pre/post-TX checkpoint recovery.415 CC replies reproduce exactly;416 MAC frames
plus the original113 fixtures pass. V1 production migration, V2 persistence,
MQTT and telemetry/discovery JSON regressions pass; PlatformIO build passes.

Flash digest verified, NVS backed up privately, capture sole serial owner restored.
Both0.12 boots were SHADOW/recovered/autoOFF. Actual V2 write/readback and passive
reset verified; this is not a physical power-cut or active recovery pass. MQTT
reconnected and HA advertises room estimate/setpoint/low-battery entities. One
startup STATUS line was interleaved with an ESP MQTT startup error; subsequent
STATUS records are intact. A fifth narrow Claude critique was checked/rejected
against the tested255 request-rejection behavior (and its ACK/ID confusion).

Next physical gate: one bounded CC-off node16-only target/local-precedence check.
No active control has started. See bridge-0.12-manifest.json,
bridge012-passive-verification.json and MANUAL-UPDATE-ID.md.

Fresh post-reset node16 report22:39:33.828 authenticated; CC22:39:33.906
confirmed22 C/context2. Its previously authenticated extended template22:28:57
was imported explicitly while passive (not counted as RX); latest CC context and
target match. STATUS22:40:05 verifies known context/config, all disabled/no
pending,autoOFF, zero bridge TX. MQTT carries22 C target,19.89 C room estimate,
low_battery=false/status4a00. No visual application claim.

Requested physical CC-off confirmation for one short spare-only test. Until that
arrives, Heltec remains passive. Prepared/tested an independent guard which queues
atomic SHADOW/STATUS, requires fresh saved-disabled status and does not mistake
queue time for actual stop time. Guard is not started yet. Fixed read-only MQTT
probe keepalive for listens longer than30s; firmware MQTT was unaffected.


## 22 October — decisive spare result and0.13 release candidate

User confirms CC off.0.12 spare16 trial08:38:16–08:42:36: physical MQTT19 C
applied, local22 C stayed with no alarms;5/5 matching ACKs. All other heads
remained disabled. Saved SHADOW verified, guard ended, user restored CC.

0.13 implements write-ahead context/target pairing in V3, opt-in exact-intent
recovery, per-head MQTT/climate support for seven paired heads, short-poll
handling for5, and separate battery-sample freshness. Six sanitizer host suites
and PlatformIO pass. Flashed/hash verified; boot08:52:09 is SHADOW/recovered,
autoOFF; Wi-Fi/MQTT up. No0.13 active control has occurred.

Next is commissioning, not additional feature expansion: spare active reset and
write timing, then paired-head takeover at existing setpoints and coverage.
Requested CC-off confirmation explicitly covering all seven paired IDs.
ID overflow255 still fails closed and remains a release limitation; no claim of
indefinite or whole-house acceptance. Full evidence/rollback paths in
RELEASE-CANDIDATE-0.13.md and bridge-0.13-manifest.json.
