# Manual update-ID candidate — bridge 0.12

Observed rule: genuine manual changes advance byte19 once while preserving
opaque bytes16..18. Four retrospective actions and one prospectively predicted
node16 action fit this model. This is not proof of receiver/application semantics.

Implementation:
- Remote SET stores the desired target/revision. Allocate the ID when its first
  reply is built; replacing an unsent target consumes no ID.
- Bind the issued ID to that target. Retrying the same target keeps its ID.
  A different target after build gets a new ID, even if the old ACK was lost.
- Reuse a duplicate reply cache only for the same target revision. An old ACK
  records its own delivered target and cannot consume a newer queued request.
- An enabled head's new local target cancels the queued request/cache and gets
  one new ID. Repeated reports of the same target do not increment it. Passive
  observation leaves allocation entirely to the real CC.
- Reject a different remote target at byte255. A local change at exhaustion
  stops bridge control and requires fresh CC context; never carry into flags.
- Extended manual replies set both leading target words and retain bytes24..31.
  Short replies echo the current target/context. These remain candidate rules.

## Restart and handoff

0.12 is a deliberately bounded spare-only bench candidate. CONTROL ON permits
only node16. Every boot disables ownership and invalidates recovered CC context
and configuration. AUTOSTART ON is refused. Fresh authenticated CC observations
(or an explicitly imported genuine extended reply) are required before enabling.
After issuing a manual ID, stopping control also invalidates its context/config.
Accepted targets remain available as recovery/telemetry data.

Reason: a head may apply an unacknowledged command immediately before a crash.
The last saved ID may then be stale. Resuming from it can reuse an ID for another
target. Durable write-ahead ID reservation is NOT implemented; earlier0.5
successful reboot tests do not establish recovery safety for this new policy.
No automatic restart/recovery acceptance is claimed for0.12.

## Verification

ASan/UBSan scenarios cover the prospective17→22/1→2 trace, unchanged upper bytes,
replacement before/after build, identical requests/retries, old/lost ACKs, local
precedence, local echo of a remote target,255 rejection and both pre-TX and post-TX
checkpoint recovery. Deterministic request/local/ACK interleavings cover all256
starting ID bytes and check that no issued ID binds to different targets.
Existing V1/V2 persistence, MQTT, telemetry and codec suites
also pass.415 genuine CC replies reproduce exactly;416 frames validate MAC.
PlatformIO heltec_v3 build passes. Tests establish our state machine, not head
application. See bridge012-*-result.* and bridge-0.12-manifest.json.

A fifth narrow Claude question reviewed the state model with no files/key/tools.
Its proposed255 failure is rejected: the implementation rejects the replacement
request at255, explicitly tested; it also confused the radio ACK sequence with
the application update ID. The critique supplies no new protocol evidence.

## Next physical gate

One short CC-off node16-only window: remote target changes on the head, followed
by a local target above it without lock/alarm; confirm on-air ID and preserved
configuration. Stop to saved SHADOW and verify before CC restoration. No hour
trial is required merely to test this field hypothesis. Wi-Fi outage, durable ID
recovery and household migration remain separate gates.

Reproduce the state-machine scenarios:

```sh
c++ -std=c++11 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -fno-omit-frame-pointer tools/test_manual_updates.cpp -o /tmp/danfoss-manual
/tmp/danfoss-manual
```
