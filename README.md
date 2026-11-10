# Danfoss Link CC replacement on a Heltec V3

Firmware for a Heltec WiFi LoRa 32 V3 (ESP32-S3 + SX1262) that takes over the
radio job of a Danfoss Link CC for radiator heads already paired to it, and
exposes each head to Home Assistant over MQTT. The heads keep regulating their
own valves; the bridge sends them targets and reports what they measure.

Not affiliated with or endorsed by Danfoss. "Danfoss" and "Danfoss Link" are
Danfoss A/S trademarks. The protocol was worked out from radio captures of the
author's own installation, for interoperability. It controls heating: use it at
your own risk and keep your CC to fall back on.

**New installation:** start with [Getting started](docs/GETTING-STARTED.md).

## How it works

- The heads talk on 868.40 MHz, FSK at 40 kbit/s, in a Z-Wave-like frame format
  (ITU G.9959) with Danfoss's own commands.
- Heads run on batteries and sleep. Every few minutes each wakes up and reports;
  the controller answers in that short window with the target. A change from HA
  therefore lands at the head's next wake, not instantly.
- Every command carries a 4-byte tag: an XTEA CBC-MAC under a 16-byte key unique
  to each Link network. The CC sends that key in clear when it pairs a head, which
  is how you get yours ([details](data/AUTHENTICATION.md)).
- The bridge answers each head's report with the target from Home Assistant,
  saves its state in flash so a restart changes nothing, publishes MQTT discovery
  for thermostats, room temperature, battery and valve diagnostics, and updates
  over Wi-Fi.

## Read these documents first

| Document | Contents |
|---|---|
| [Getting started](docs/GETTING-STARTED.md) | Capture your Home ID and key, configure, flash and take over from the CC |
| [Firmware manual](docs/FIRMWARE.md) | Hardware, radio/protocol, authentication, tasks, state machine, local precedence, persistence format, recovery, command reference, source map and tests |
| [Home Assistant manual](docs/HOME-ASSISTANT.md) | Installed integration, rooms/devices/entities, complete MQTT contract, availability, dashboards, example schedules/alerts, backup and troubleshooting |
| [Operations manual](docs/OPERATIONS.md) | Current deployment, serial ownership, command-spool recipes, commissioning, restart, build/flash/rollback and incident procedures |
| [Documentation/evidence index](docs/INDEX.md) | Authoritative files, historical investigations, acceptance evidence and maintenance rules |

These manuals describe the implementation, not a promise that every inferred
protocol field is understood. Examples are documentation only: schedules,
dashboards and notification automations shown in the HA manual have **not** been
installed by writing these files.

## Reference installation

The author's house, documented throughout the manuals (Home ID `ffd2bb2d`).
At the recorded commissioning checkpoint, **09:22:06 on 22 October 2026**:

- CC off; Heltec active, AUTOSTART on, clean V3 checkpoint generation 76.
- Six reachable heads acknowledged replies before and after a saved-state EN reset.
- Seven node IDs configured for control; deleted spare 15 excluded.
- Seven named devices/areas and 28 discovery entities verified in HA.
- Small bedroom/node 5 is an inferred mapping and has not communicated during
  takeover. The user reports low battery and connection alarms there and has
  deferred fresh batteries/central placement.

| Room | Node(s) | Saved target at commissioning |
|---|---|---|
| Living room | 2, 6 | 16 °C each |
| Small bedroom — provisional | 5 | 16 °C |
| Bedroom 3 | 8 | 18.5 °C |
| Master bedroom | 12 | 18 °C |
| Guest room | 13 | 16 °C |
| A — working spare | 16 | 17 °C |

These are dated observations, not live status. See
[the commissioning handoff](data/COMMISSIONING-20261022.md) and
[machine-readable room mapping](data/room-map.json).

## Acceptance boundaries

This is a deployed release candidate. The low byte of the manual update context
**fails closed at 255** rather than using an unverified rollover rule. V3 recovery
has passed hardware reset checks; physical power interruption during V3 writes
and simultaneous radio/flash stress remain unaccepted. Earlier firmware's physical
power-cycle result does not establish those V3 properties.

Do not power the CC while Heltec owns radio control. To return to CC operation,
first release and durably verify bridge ownership as described in
[operations](docs/OPERATIONS.md#return-control-to-the-cc).

### Files kept out of git

The radio network key lives only in the macOS Keychain. The firmware build and
the tools read it from there, and a pre-commit hook blocks it in any encoding
(see [Network key](docs/OPERATIONS.md#network-key)). Docs and captures carry
`<network-key>` or a blanked capture `hex` where the key used to be. `.gitignore`
also keeps the WiFi/MQTT credentials (`*.private.*`), the `private/` originals and
all `.bin`/`.elf` images and NVS dumps out of the repository, because those embed
the key and passwords. Your Home ID and rooms live in
`firmware-bridge/src/household.private.h` and `ha/household.private.json`. A fresh
clone needs the Keychain item, those files,
`firmware-bridge/src/network_config.private.h` and
`git config core.hooksPath .githooks`.

Large raw captures, build logs and old source backups from the research are not
published; documents that cite them say so.

## License

MIT, see [LICENSE](LICENSE). `firmware-test/lib/opendroneid` is Apache-2.0.
