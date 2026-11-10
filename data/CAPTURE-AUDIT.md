# Raw capture audit — 20 October 2026

All times Europe/Copenhagen (UTC+02:00). User reports pairing one physical head,
and possibly removing an already-dead head afterward.

## Conclusion

The capture supports two setup exchanges under node IDs 14 and 15, **not two proven
physical heads**. Node 14 stops after an op=90 exchange; node 15 proceeds into further
configuration. A retry/restart using the same head is consistent with these observations.
The later unresponsive-device exchange is addressed to **node 3**, not node 15.
That is the strongest match for the reported dead-head removal, although the proprietary
opcode meaning and CC UI action cannot be proven from this recording alone.

## Provenance and coverage

Read-only SQLite query of `rpi5:~/listening-post/lp.db`, table `raw`, selecting
2026-10-20 23:20 inclusive through 23:50 exclusive, ordered by timestamp. Export retained
as `raw-20261020-2320-2350.jsonl`: each row preserves database timestamp and parsed receiver
JSON. This is raw receiver-window data, not IQ samples or a complete continuous RF recording.
267 records: 265 zw40t byte windows and two metadata records. Actual exported records end
at 23:33:08.832; the filename describes the requested query window, not uninterrupted coverage.
No zw96 byte windows are present in this export, so it cannot establish absence of R1 traffic.

`python3 tools/audit_capture.py data/raw-20261020-2320-2350.jsonl` recreates the known-network
frame listing. The scanner examines both polarities and all eight bit alignments for Home ID
ffd2bb2d, checks declared frame length and XOR checksum, and retains multiple frames per window.
It finds 93 occurrences (80 distinct frame byte strings); the old first-hit decoder finds 73.
Every frame in the original 37-line extract is present and passes length/checksum checks.
Every recovered alignment was additionally verified by independent literal-bit extraction.
XOR validation is not cryptographic authentication. Unknown/zero Home IDs are outside this
scanner's scope; missing frames and missing replies do not establish that nothing was transmitted.
Timestamps are database receiver-window timestamps; frames within a window share that timestamp.

## Timeline with raw row references

| Local time | Raw rows | Observation |
|---|---:|---|
| 23:27:53.158–.743 | 134–141 | Node 14 range exchange, manufacturer query, proprietary `80 02 10`; bidirectional replies. |
| 23:27:53.855–.896 | 142–144 | CC sends `80 04` plus 16 bytes; head sends `80 05 00`, then a status/config response. |
| 23:27:53.949–54.141 | 145–148 | CC sends op=50; head sends `90 03 20 720a9b95`; CC sends `90 04 21 00 c7d6338e`; head ACKs. |
| 23:27:54.234–.363 | 149–151 | Three CC NO_OPERATION occurrences to 14, same sequence; no reply recovered. |
| 23:28:22.360–23.130 | 157–168 | Similar range/manufacturer/80 setup with node 15; same 16-byte value, head response, CC config. |
| 23:28:23.188–25.275 | 169–178 | Routing/configuration continues for 15, with head replies and CC ACKs. No op=90 exchange with 15 recovered. |
| 23:29:18.127–.262 | 198, 200 | CC sends `90 03 22 c9dd0983` twice to **3**. |
| 23:29:23.412–.652 | 202–207 | Six NO_OPERATION occurrences to **3**, sequences 2 and 3, with no recovered reply. |

## Corrections to the previous interpretation

- The original extract was sorted by displayed text within seconds, not precise event time.
  For both 14 and 15 the 16-byte transmission precedes the CC's op=50 config block.
- `22c9dd0983` belongs to node 3. It is not node 15's per-node seed.
- op=90 may concern setup failure, cancellation, or removal. Its leading body values 20,
  21 and 22 are candidates for command/status meanings; calling the entire body a nonce
  is unsupported. Node 14 initiates its op=90 exchange, unlike the later CC-to-3 exchange.
- op=80's following byte cannot consistently be a length: 04 precedes 16 bytes, whereas
  02 and 05 precede one byte each. The display now uses neutral `field=` for this byte.
- The repeated 16 bytes are a plausible key candidate. Same bytes sent to two addresses
  do not establish two physical devices, network-wide reuse, or successful authentication.
- Failed CMAC/CBC-MAC trials cannot establish hidden counters or S0/S2. The key itself,
  algorithm, data coverage, and interpretation of the four trailing bytes remain hypotheses.
- Standard protocol commands 04, 06, 07, 0c and 14 mean Find Nodes In Range, Range Info,
  Command Complete, Assign Return Route and Assign SUC Return Route. They are not Assign IDs
  (03) or Node Information Frame (01). Checked against the primary Z-Wave JS implementation:
  https://github.com/zwave-js/node-zwave-js/blob/master/packages/cc/src/lib/_Types.ts
  https://github.com/zwave-js/node-zwave-js/blob/master/packages/cc/src/cc/ZWaveProtocolCC.ts

## Next useful experiment

For further protocol work, capture one labelled head from before setup starts through its
first normal report, recording the CC result and any retry/reset times. Perform dead-head
removal separately with its own timestamp. Retain all receiver windows, including other
Home IDs. This can distinguish a transient node ID from another device and separate
successful setup from removal/error messages. Existing capture alone cannot settle those points.
