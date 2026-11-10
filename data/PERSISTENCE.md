> Historical persistence log. The authoritative current V3 format, write-ahead intent and recovery rules are in [the firmware manual](../docs/FIRMWARE.md#persistent-state-and-recovery). Older pending-exclusion and passive-only policies below apply to their stated versions.

Current (22 October): 0.13.1 uses V3 exact issued-context/target checkpoints,
with write/readback before application TX and opt-in AUTOSTART recovery.
Active spare firmware restart recovery passed with a matching ACK. V3 physical
power-cut/interrupted-save acceptance remains open. See RELEASE-CANDIDATE-0.13.md.
The older storage/firmware descriptions below are historical.

> Current0.12 bench policy: automatic recovery is intentionally disabled and fresh
> authenticated CC context/configuration is required after every boot. The earlier
> recovery results below are historical; they do not validate manual-ID recovery.
> See [MANUAL-UPDATE-ID.md](MANUAL-UPDATE-ID.md).

# Bridge 0.5 persistence/recovery — local implementation

Bridge 0.5 flashed and digest-verified on 21 October 2026 at about 14:44.
The completed 0.4 radio trial is archived separately; its verified image remains
`bridge-0.4-verified.bin`. The current 0.8 build retains this storage format and is in saved SHADOW; see BRIDGE-PROGRESS.md for deployment state.

## Behavior

- Store head IDs, enabled flags, last accepted targets, mode bytes and an explicit
  global automatic-resume setting. Network identity and a key-derived tag bind the
  record to the compiled network. The actual network/key are still compiled;
  provisioning a new network/key remains separate work.
- Accepted means an authenticated local/CC observation or a target delivered in a
  link-ACKed reply. A link ACK does not prove the thermostat applied the target.
- Pending external targets, packet sequences, retransmission caches and counters
  never enter storage. Reboot uses the last accepted target, not an unacknowledged
  SET; it does not queue an old extended command.
- Fresh, unreadable, wrong-network and unsupported-version records boot SHADOW.
  Saved targets can be restored while control stays disabled.
- Automatic control after reboot requires explicit `AUTOSTART ON` and at least one
  enabled head with an accepted target. Wait for the subsequent `saved` event;
  the command's initial acknowledgement only means the save is queued.
- `SHADOW`/`STOP` disables all replies and automatic resume, then checkpoints before
  reporting `recovery_disabled_saved:true`. Do not restore the CC if that field is
  false: the old boot ownership may still exist. Individual CONTROL OFF and
  AUTOSTART OFF also attempt an immediate checkpoint.
- `SAVE` queues a checkpoint; `STATUS` exposes storage readiness, generation,
  automatic-resume setting and dirty state. A `saved` event confirms write/readback
  validation. No checkpoint is written for routine packet counters.

## Storage and write timing

`persistent_state.h` is a portable codec with explicit bytes, version 1, exact length,
network binding, input validation and CRC32. `nvs_store.h` uses the Arduino Preferences
API with two NVS blobs (`danfoss/state0` and `state1`). Write the inactive slot and
validate readback. Select the newest valid generation on boot; a torn/corrupt newer
slot falls back to the prior valid record. Both invalid means SHADOW.

Normal writes coalesce changes for five seconds and wait for one second without
an authenticated radio event. Flash writes run in the console/loop, never inside
the radio task. ESP32 flash operations can still stall execution: quiet-time gating
reduces this risk but does not establish deadline performance. Hardware stress
validation is required. Explicit ownership-disable checkpoints prioritize durability.

The most recent accepted change can be lost if power fails before its checkpoint.
A target changed locally while the bridge is fully offline might not be recoverable
from routine reports; this implementation does not claim to reconstruct lost events.

## Verification completed

- Host codec/store tests under AddressSanitizer and UndefinedBehaviorSanitizer:
  shadow/active recovery, pending-command exclusion, no stale sequence replay,
  every truncated prefix, maximum-size record, CRC/version/network/key binding,
  duplicate node/range rejection, corrupt newest slot, torn/failed writes,
  generation-wrap ordering and unavailable storage.
- Existing bridge protocol/state regression passes with all 113 MAC fixtures.
- Python authentication suite: three tests pass.
- PlatformIO heltec_v3 build passes. Actual ESP32 flash/power-loss behavior and RF
  timing during writes remain hardware acceptance items.

Reproduce:

```sh
c++ -std=c++11 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -fno-omit-frame-pointer -I tools/fakes tools/test_persistence.cpp \
  -o /tmp/test-bridge-persistence
/tmp/test-bridge-persistence
pio run -d firmware-bridge
```

## Next hardware test (after the unchanged 0.4 trial)

1. Back up state/firmware and flash 0.5; confirm fresh SHADOW and storage ready.
2. In a CC-off spare-only session, enable node 15 with a modest target. Verify its
   reply ACK and wait for `saved`. Keep every other head disabled.
3. Send AUTOSTART ON; wait for `saved` with autostart=true and note the generation.
4. Power-cycle Heltec. Confirm only node 15 resumes, last accepted target survives,
   pending=false, packet sequences/counters fresh, and sustained replies/ACKs recover.
5. Change the target locally, wait for checkpoint, reboot again and verify the local
   target persists. Test a queued-but-undelivered SET and confirm it is not replayed.
6. Send SHADOW; require recovery_disabled_saved=true, then reboot to confirm it stays
   passive before restoring the CC. If NVS reports a write error, resolve it first.
7. Separately exercise interrupted saves and simultaneous incoming reports while
   saving. Compare processing/reply timing and retries with the 0.4 baseline.

After this gate, add Wi-Fi/MQTT/HA. The production reboot promise is not accepted
merely because host tests and a firmware build pass.

## Hardware results so far — 14:46

- CC restored by user; normal operation confirmed and authenticated CC traffic
  observed before the next firmware test.
- Actual partition layout checked and NVS/OTA metadata backed up before writing.
- 0.5 flash digest verified. Real NVS checkpoint write/readback succeeded.
- SHADOW reports recovery_disabled_saved=true. ESP32 hardware reset through
  esptool then boots recovered=true, autostart=false and shadow=true.
- Node 12's saved 18 C target is present before any new CC reply to that node.
  Generation 2 survives; all heads stay disabled. Evidence: persistence05-passive-
  before.json and persistence05-passive-after.json.
- This reset did not disconnect USB power. Physical power-cycle and active
  automatic recovery remain untested. Awaiting CC-off confirmation for node 15.

## Active reboot and pending-command exclusion passed

With explicit user confirmation that CC is off, only node 15 was enabled at 17 C.
Its reply ACK at 15:02:21 was followed by a generation-7 checkpoint; AUTOSTART ON
was saved as generation 8. A new SET 15 18 was then verified pending and never
transmitted; SAVE reported the accepted checkpoint unchanged.

After an ESP32 hardware reset, boot reports recovered=true/autostart=true. STATUS
confirms exactly node 15 active, target=1700 and pending=false, with generation 8.
The undelivered 18 C command was excluded. This was a hardware reset, not a USB
power disconnect. Evidence: persistence05-queued-before-reset.json and
persistence05-active-after-reset.json.

Next: user changes locally to 18 C; verify its saved checkpoint, then perform a
physical Heltec power-cycle. Finish with durable SHADOW/reboot before CC restore.

## Local target saved — 15:17

User confirms local 18 C holds without a connection warning. The authenticated
button report and head ACK are captured, generation 9 is saved, and a subsequent
STATUS confirms clean storage, target 1800, no pending command, and only node 15
enabled. Awaiting a physical Heltec power disconnect/reconnect.

## Physical power-cycle and final passive recovery passed — 15:22

User confirmed five seconds without Heltec power. Boot restores generation 9,
local 18 C and only node 15 enabled with no pending command. A resumed reply
receives a matching head ACK. SHADOW then saves generation 10; a further hardware
reset restores SHADOW, AUTOSTART OFF and every head disabled. No bridge TX after
that passive boot. User asked to restore CC; confirmation pending.

Normal recovery, pending-command exclusion and local target persistence pass.
Interrupted-save hardware behavior and simultaneous RF/write stress remain open.
The session report includes deliberate resets and is not a new hour trial; its
27 malformed records are non-JSON boot/console lines, not 27 failed radio frames.

## CC restoration complete — 15:53

User confirms normal household operation; 40 authenticated CC packets observed.
Heltec remains passive with all heads disabled and AUTOSTART OFF. Normal recovery
bench closed. Hardware interrupted saves and RF/write timing stress remain open.

## Ownership-release cancellation in 0.8

SHADOW and CONTROL OFF now discard transient desired/pending targets, invalidate
reply/ACK caches and clear queued MQTT requests. Desired target reverts to the
last accepted target, allowing passive CC learning again. Previously a pending
RAM request could survive SHADOW until reboot and block CC learning or be reused
on a later enable. The existing durable snapshot already excluded pending targets.
Host sanitizer tests and passive hardware SET-then-SHADOW/OFF checks pass; all
heads stayed disabled and no transmissions occurred. Generation 15 retained.

## 0.9 configuration-template limitation

New extended commands require genuine per-head CC configuration learned this boot;
bytes22..31 are preserved rather than filled with the target. This template and
room/battery telemetry are RAM-only. Existing NVS schema still restores accepted
target/mode/ownership and excludes pending commands. New writes are refused after
a standalone reboot until configuration is learned again. Persisting and validating
these templates, with migration/recovery tests, is required before relying on0.9
as a standalone whole-house replacement. Do not call the firmware finished yet.

## Prepared0.11 V2 storage (not flashed)

V2 stores the full opaque4byte CC context and10byte authenticated configuration.
Maximum record312bytes; same CRC/network/key binding and inactive-slot/readback
strategy. V1 records are accepted for migration but force passive/no autostart,
because they cannot recover the context bytes they discarded or missing template.
V2 recovery can restore learned context/config; pending targets/sequences and
sensor readings remain transient. Autostart requires known context/configuration.

Host tests pass for context00000101/template recovery, every-byte corruption and
truncation of the test record, torn first V2 write with V1 passive fallback, and
corrupt latest V2 fallback. A real generation30/eight-head V1 record extracted
from the pre0.10 NVS backup also migrates/roundtrips in the host codec. This does
not establish physical V2 save/reboot or electrical interruption on hardware.
