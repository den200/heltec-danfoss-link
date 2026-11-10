# Deployment handoff — 22 October 2026

Firmware 0.13.1 is the active controller; CC remains OFF. AUTOSTART ON,
checkpoint generation 76, seven paired nodes enabled. Deleted node 15 disabled.
All configured targets retained. Saved reset recovery passed for the six reachable
heads at 09:22:06. This was an EN reset, not a physical power cut.
The temporary shutdown guard is stopped. Serial capture remains its sole owner.

| Room | Node | Target | Radio verification |
|---|---:|---:|---|
| Living room | 2, 6 | 16 C each | Both ACKed before and after reset |
| Bedroom 3 | 8 | 18.5 C | ACKed before and after reset |
| Master bedroom | 12 | 18 C | ACKed before and after reset |
| Guest room | 13 | 16 C | ACKed before and after reset |
| A | 16 | 17 C | ACKed before and after reset; earlier remote/local physical pass |
| Small bedroom (inferred) | 5 | 16 C | Unheard; user reports low battery and connection alarm |

User explicitly deferred Small bedroom batteries/central placement. Do not infer
successful coverage from the other heads. Office has no mapped active head.
HA names/areas are verified visually. Each head exposes climate, setpoint,
room-temperature estimate and low-battery entities. Unheard sensors stay unknown.
Room mapping confidence and reference readings are in room-map.json.

Remaining firmware acceptance: low-byte update ID255 fails closed; wrap semantics
remain unvalidated. V3 interrupted-write/physical power-cut behavior remains open.
The firmware is therefore a deployed release candidate, not an indefinitely
accepted replacement. No custom HA app has been built; named MQTT entities exist.

If restoring CC, first queue SHADOW through the capture command spool and verify
all heads disabled, no pending requests, saved AUTOSTART OFF/clean storage.
Never operate both controllers together. Rollback paths and hashes are retained
in bridge-0.13.1-manifest.json; private NVS/binary backups remain on the Pi.

Evidence: commissioning-0131-recovery.json, commissioning-0131-mqtt.json,
commissioning-013-report.json, commissioning-20261022-raw.jsonl,
ha-room-mapping-20261022.png.
