# Spare16 application command investigation

Status: radio delivery proven; replacement application control is NOT accepted.
CC is restored; Heltec0.10 stays saved SHADOW. No live test is running.

## Captured differences

| Exchange | Primary/secondary C | Raw header word16..19 | Physical result |
|---|---|---|---|
| Genuine CC,19:34 |21/21|00000003|User confirms normal CC/head synchronization|
| Genuine CC after local20.5 |20.5 (short), then20.5/20.5|00000004|Normal synchronization|
| Genuine CC19:35 |19.5 (short), then19.5/19.5|00000005|Normal synchronization|
| Genuine CC19:51 |20/20|00000006|User confirms20 C applied|
| Bridge0.9,19:55 |19/20|00000006|User checked during run: still20 C|
| Bridge0.10,20:01 |19/19|00000006|ACKed; no timestamped in-run display confirmation. Later report:20 C/alarms after guard could already have stopped control|

Both bridge commands target radio16; both receive source16 link ACKs. Each uses
the report's application sequence.0.10 retains the same tail as the genuine CC:
`0258 0af0 0000 0af0` (raw words600/2800/0/2800). Neither radio ACK nor valid MAC
establishes that the head applied its payload.0.9 primary-word-only is insufficient;
0.10 two-word change remains unaccepted, not proven a radio failure.

The most useful remaining difference is the raw header word. We currently call
its low byte modeByte and reuse it on new commands. Genuine manual changes above
advance3→4→5→6. That supports a change/revision/request-context hypothesis. Other
heads' apparent scheduled/context changes keep this word unchanged (10 of16
captured target transitions), so blindly incrementing it for every reply would
not match the data. A later broad audit found00000101: upper bytes are NOT always
zero.0.10 discarded those bytes; prepared0.11 preserves the complete opaque
context. Do not call it a proven single32bit counter or decoded heating mode.

Related genuine CC43-byte requests follow some updates and receive head22-byte
`0d00` responses. Their complete role also remains unresolved; bridge only mimics
the report/reply path. Startup clock/configuration exchanges must be distinguished
from target application before excluding them as irrelevant.

## Next offline work

1. Correlate local-button reports, CC manual changes, this header word, extended
   responses and follow-up43-byte requests as complete exchanges.
2. Model a new manual request separately from duplicate retransmission and routine
   replies. Check against held-out CC exchanges, preserving opaque configuration.
3. Prepare a candidate only after that model fits; keep runtime in SHADOW. Persist
   validated configuration/reply context with recovery tests before standalone use.
4. A later physical check would still be required to establish application behavior;
   no further CC-off action is requested now.

Evidence:spare16-command-comparison.json,cc-target-mode-transitions.json,
bridge09-active-*,bridge010-active-*,cc-restored-after010.json.
Latest findings and prepared fixes:INVESTIGATION-20261021.md.
No firmware/key brute force, source change, flash or RF control in this offline pass.
