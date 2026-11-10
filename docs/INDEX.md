# Documentation and evidence index

Documented against source and deployment evidence for bridge 0.13.1 on
22 October 2026. Local times are Europe/Copenhagen unless an explicit offset is
shown. The manuals are intended for both the operator and a future developer.

## Authority and navigation

1. [README](../README.md): entry point and dated deployment summary.
2. [Firmware](FIRMWARE.md): current executable behavior and source references.
3. [Home Assistant](HOME-ASSISTANT.md): current discovery/MQTT contract and HA use.
4. [Operations](OPERATIONS.md): maintenance procedures and rollback.
5. [Commissioning handoff](../data/COMMISSIONING-20261022.md): actual deployment,
   coverage exception and evidence at the end of this commissioning window.
6. [Progress log](../data/BRIDGE-PROGRESS.md): chronology; its top section is current,
   lower sections are historical.

Published copy: about 56 MB of raw radio captures (`*-raw.jsonl` and similar),
the build/flash logs, old source backups, HA screenshots and assistant
question/answer notes were left out of the public repository. Documents below
still cite some of them by name; the summaries and results in those documents
are what was published.

For behavior, the checked-in C++ source is definitive. For a hardware result,
use the capture/session evidence and labelled user observation. For the currently
running binary, use the manifest/hash plus the latest boot event; source alone
does not establish what is flashed. A dated status snapshot is not live telemetry.

## Current implementation and release evidence

| File | Purpose |
|---|---|
| [Release candidate](../data/RELEASE-CANDIDATE-0.13.md) | Write-ahead design and outstanding acceptance gates |
| [0.13.1 manifest](../data/bridge-0.13.1-manifest.json) | Binary/source hashes, rollback paths and acceptance boundaries |
| [Recovery evidence](../data/commissioning-0131-recovery.json) | 09:17 reset, exact recovered ownership/targets, six post-reset ACKs |
| [Session](../data/commissioning-013-session.json) | Authorization, test phases, guard cancellation and deferred node 5 |
| [Session report](../data/commissioning-013-report.json) | Node-16 trial accounting; not a seven-head physical application certificate |
| [Commissioning journal](../data/commissioning-20261022-raw.jsonl) | Local copy of timestamped console events |
| [MQTT evidence](../data/commissioning-0131-mqtt.json) | Broker round-trip and post-reset per-head telemetry |
| [Room map](../data/room-map.json) | Room identity, screenshot/radio references and confidence |
| [Retained discovery](../data/room-discovery-live.json) | Published discovery messages |
| [HA screenshot](../data/ha-room-mapping-20261022.png) | Named head devices and assigned areas verified visually |
| [0.13.1 build log](../data/bridge0131-build.log) | Build result |
| [0.13.1 flash log](../data/bridge0131-flash.log) | Flash/digest verification |

The remote authoritative capture is on `rpi5` at
`~/danfoss-captures/bridge-20261021/raw.jsonl`. The date in the directory
name is the directory's creation date, not a limit on the journal's contents.

## Historical research: retain, but do not use as the current runbook

| Document | What it established / why it is historical |
|---|---|
| [Protocol and plan](../PROTOCOL-AND-PLAN.md) | Original protocol investigation and staged integration plan |
| [Notes](../NOTES.md) | Early observations and hypotheses |
| [Authentication](../data/AUTHENTICATION.md) | XTEA CBC-MAC recovery/validation; contains private authentication material |
| [Control success](../data/CONTROL-SUCCESS.md) | Initial physical target application |
| [Timed responder](../data/TIMED-RESPONDER.md) | Early responder/timing implementation |
| [Transmission test](../data/TRANSMISSION-TEST.md) | Early RF checks |
| [Persistence](../data/PERSISTENCE.md) | V1/V2 history and old recovery policy; current V3 is in the firmware manual |
| [MQTT stage](../data/MQTT.md) | Broker installation and 0.7–0.9 tests, including now-deleted spare 15 |
| [Battery status](../data/BATTERY-STATUS.md) | Labelled low-battery and replacement observations |
| [Head temperatures](../data/HEAD-TEMPERATURES.md) | Temperature decoding evidence and historical estimates |
| [Manual update ID](../data/MANUAL-UPDATE-ID.md) | Prospective CC comparison and 0.12 state-machine policy |
| [Spare command analysis](../data/SPARE16-COMMAND-ANALYSIS.md) | Failed target application and configuration analysis |
| [Firmware analysis](../data/FIRMWARE-ANALYSIS.md) | Official firmware-package investigation; not the Heltec implementation |
| [NVS bench](../data/NVS-BENCH.md) | Isolated storage experiments |
| [Capture audit](../data/CAPTURE-AUDIT.md) | Capture validity and interpretation limits |
| [Investigation](../data/INVESTIGATION-20261021.md) | Previous investigation checkpoint |

Old instructions such as “node 15 only,” “all pending requests are excluded from
storage,” or “configuration required for every command” do not describe 0.13.1.
In V3, a prepared ID/target intent is intentionally durable and can resume after
reboot. Current MQTT routes cover the seven paired IDs.

## Acceptance vocabulary

| Observation | What it proves |
|---|---|
| Successful compile/host test | The tested software property for those inputs |
| `response_status:0` | RadioLib reported successful transmission |
| Matching ACK / increasing `reply_acks` | A matching link acknowledgement was received; not physical setpoint application |
| User sees requested temperature | Physical application on the identified head at that observation |
| Fresh room sample | Decoded measured estimate; not proof of valve position or heating demand |
| `saved` and clean storage | The software completed checkpoint write/readback, or confirmed identical saved state |
| EN reset recovery | Saved settings resumed after reset; not an electrical interruption test |
| Earlier firmware's test | Evidence for that version, not automatic acceptance of later storage/control changes |

Never combine separate firmware runs into one uninterrupted-duration claim. Never
equate discovery visibility with RF coverage, or infer Small bedroom coverage from
the other rooms' success.

## Updating the documentation

When firmware changes, update version references, command/schema tables, storage
format and migration notes, tests, remaining gates and the release manifest. Keep
the old manifest as evidence instead of relabelling an old hash as a new build.

When HA changes, record actual entity IDs only after inspecting the registry/UI;
room names, unique IDs, device IDs and entity IDs are different concepts. Mark
example YAML as an example until it is installed and verified. Keep credentials
out of documentation and screenshots.

When ownership changes, record the real mode, AUTOSTART and durable state. Leave
historical captures intact. A temporary test guard must either complete a verified
SHADOW stop or be explicitly cancelled with the resulting active state recorded.
