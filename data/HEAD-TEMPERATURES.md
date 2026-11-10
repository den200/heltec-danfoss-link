# Current room mapping — 22 October 2026

See [room-map.json](room-map.json) for the matched app/radio readings and
confidence. Named HA devices/areas are verified in ha-room-mapping-20261022.png.
Node 5 / Small bedroom remains inferred; no fresh temperature sample.
Node 16 / A identity is established by bidirectional controls; its screenshot
temperature differs from the later radio sample, with cause unresolved.

# Head temperature investigation — 21 October 2026

Status: provisional field mapping, published by0.9 as explicitly labelled room-temperature estimates. Exact simultaneous CC comparison remains pending.

Danfoss documents two sensors (behind the display and near the valve), from which
the thermostat calculates room temperature approximately 20 cm in front of it.
An optional Link RS can instead supply the room measurement. The head display
shows the requested target, so it is not an independent room-temperature reference.

Source: https://assets.danfoss.com/documents/latest/71025/AI099086402667en-010401.pdf

Authenticated routine 57-byte reports have three signed big-endian words at
zero-based offsets 40, 42 and 44. Dividing by 100 yields plausible Celsius values.
Working hypothesis: front sensor, valve sensor, compensated room estimate.
The third word often approximates (4*first - second)/3. This is a correlation,
not a proven exact compensation algorithm: filtering/transients differ.

Across 561 distinct report events, the median absolute residual is 0.020 C,
95th percentile 0.180 C and maximum 0.643 C.

Button and alternate layouts carry one temperature-like word at offsets 38 and
44 respectively; neither is treated as the routine room estimate. Byte offsets
are applied only after strict layout classification and MAC/checksum verification.

| Node | Last routine observation (Copenhagen) | Front candidate | Valve candidate | Room candidate |
|---|---|---:|---:|---:|
| 2 | 2026-10-21T17:37:28.815+02:00 | 21.01 | 24.57 | 19.83 |
| 6 | 2026-10-21T17:46:01.804+02:00 | 19.31 | 20.07 | 19.04 |
| 8 | 2026-10-21T17:42:56.110+02:00 | 16.26 | 16.43 | 16.21 |
| 12 | 2026-10-21T17:43:30.930+02:00 | 19.20 | 22.90 | 17.93 |
| 13 | 2026-10-21T17:45:39.400+02:00 | 16.26 | 16.28 | 16.26 |
| 15 | 2026-10-21T17:46:04.330+02:00 | 21.44 | 21.04 | 21.67 |
| 16 | 2026-10-21T00:44:00.364+02:00 | 21.33 | 21.37 | 21.41 |

Node 5 has no captured report. Node 16 is stale (last routine report overnight),
so its value above is historical, not a current temperature. Node 6 is labelled
living room; node 13 is probably master bedroom; node 15 is the spare. Other room
names remain unmapped. Latest values here are a capture snapshot, not a live feed.

User confirms only radiator heads, with no separate wall sensors. After restoring CC, compare
its explicitly measured room reading (or a nearby reference thermometer) against
these candidates. A controlled front-sensor warming/cooling observation can
distinguish sensor words. Only then add validated telemetry and freshness to HA.

Reproduce: `python3 tools/analyze_head_temperatures.py CAPTURE.jsonl ...`.
Detailed frame evidence: head-temperature-candidates.json.

A separate receive-only T-Embed window captured authenticated reports from nodes
6, 8, 13 and 15 but no node 5. This does not prove node 5 is silent: the witness
also missed some known traffic. No extra routed authenticated header was found
in that short window. Prior OOK reception was restored and fresh packets verified.

The spare was handled during the bench; its front-word rose while the valve-word
lagged, and the third word rose above the front word as expected for compensation
when front > valve. This supports the mapping but is not a calibrated thermal
experiment; allow the spare to settle before comparing an ambient reference.

## 0.9 live HA publication

As of19:49, usable spare16 is powered and reports again; it is no longer stale.
Deleted spare15 is powered off and excluded from HA advertising.0.9 publishes the
strict routine third-word estimate and its own sample age. First live spare16
value after boot21.09 C with accepted target19.5 C and normal observed battery
status. CC simultaneous reference was requested and remains pending. User's earlier
qualitative agreement supports plausibility, not exact calibration. The table
above remains the historical17:xx snapshot, not current values.

HA device shows estimate21.1 C beside separate setpoint19.5 C for node16; writable
climate also has current_temperature configured but stays unavailable in SHADOW.
After15minutes without a routine sample the value becomes null even if button or
alternate reports keep arriving. Node5 has no report and remains unavailable.

First CC comparison: user reports roomA21.1 C after CC-set20 C. Authenticated
node16 routine19:48:50 has room candidate21.09 C, which HA displays21.1 C.
This is a supportive near-time reference for node16; user-report timing is not
precisely synchronized. A later19:51:46 report rises to21.34 C after handling.
Do not describe these as simultaneous calibrated measurements of all heads.
