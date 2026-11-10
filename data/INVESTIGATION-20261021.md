# Investigation: CC context and ignored setpoint

Latest:0.12 implements the candidate manual-ID rule and is passively deployed.
CC remains ON, all control disabled. See MANUAL-UPDATE-ID.md for implementation,
recovery restriction and verification. The0.11 preparation below is historical.

## Findings with direct evidence

- A real information-loss bug: authenticated CC context00000101 was read/stored
  as byte01, and outgoing replies zeroed the other3bytes. Four0x10 and one0x50
  unique target replies in the audited sample contain upper context bits.
- Manual local actions on nodes6 and16 were each followed by a CC reply echoing
  their target and advancing the low context byte by1. Two separately labelled
  CC changes on16 also advance it. This is4/4 retrospective consistency, not a
  blind validation or proof of receiver behavior.
- The upper context byte also toggles independently00000001→00000101→00000001
  for the same two living-room candidates without changing target. A single
  strictly monotonic32bit counter is not established. Preserve all bytes opaque.
- CC node6 local17 C is followed by extended replies with A21/B17/context2 on
  both6 and2. Previously A21/B16/context1. This supports a distinction between
  baseline/scheduled and stored manual targets, but exact selection/expiry rules
  remain unproved. Do not treat both temperature words as interchangeable.
- Of33 observed0x50 target replies,32 have a43byte same-head follow-up within2s;
  none of4360x10 replies does. The exception is node13 during battery recovery.
  A pending-data flag is plausible, not decoded semantics. The successful CC20 C
  command on16 used0x10 without that follow-up, so the missing43byte message is
  not a demonstrated universal requirement for a manual change.

## Source corrections prepared, not deployed

0.11 retains full context16..19 and the authenticated configuration. The lossless
codec now accepts both observed reply operations and unequal A/B words. Override
policy remains explicitly separate in the caller: no guessed revision increment
or unverified semantic rule was installed. Current two-word candidate policy
remains unaccepted for application control.

V2 A/B storage persists context, its known flag and configuration. V1 migration
recovers targets but forces passive/no autostart: V1 irreversibly lost the upper
context and had no template. Pending targets, packet sequences and sensor samples
remain excluded. New control requires known context; safe reboot ownership also
requires the recovered configuration. The legacy decoder still rejects invalid
flags/targets rather than normalizing corrupt records into valid ones.

Verification:
-408 unique genuine authenticated CC replies reproduced408/408 byte-for-byte,
  including distinct A/B words,10/50 and all5 upper-context examples.
- Original113 MAC fixtures and explicit context0101 golden frame pass.
- ASan/UBSan codec, persistence, MQTT and telemetry checks pass; new storage checks
  cover every byte of the test V2 record, truncation, torn V1→V2 save and fallback.
- Actual generation30/eight-head V1 record extracted from our pre0.10 NVS backup,
  CRC/network/key-tag checked, decoded and roundtripped through V2 passively in
  the host test. This is not a physical V2 flash-write/power-cut test.
- PlatformIO build passes. Prepared binary/manifest are retained privately.

## Claude cross-checks

Used local`claude` for4 short, specific questions with tools/MCP disabled, neutral
working directory and no project files, credentials or radio key supplied. Exact
prompts/answers are saved as claude-question-* / claude-answer-*.

Its suggestions were treated as hypotheses. Rejected its first answer's claim
that a link ACK establishes authentication; it does not. Rejected the second
answer's unsupported A/B labels and suggestion that B was "settling": our own
code changed B. The packed-context answer correctly distinguished independent
byte variation from proven field semantics. The follow-up-flag answer agreed
that the correlation does not prove causality or justify changing transmitted bits.

## Remaining uncertainty and next observation

The ignored19 C request reused the CC's manual-context value6. A new manual
request advancing the low byte once, with repeats/routine replies retaining it,
is the leading model. Overflow/reset behavior, pending-command races and head
application acceptance remain unknown; no physical pass is claimed.

The prospective passive check MATCHED. Before the action, context00000001 and
17 C were recorded and next manual context00000002 was predicted. User confirmed
starting17 C and selected22 C. Authenticated node16 button report22:28:29.883
contained22 C; CC replied22:28:29.960 with22 C/context00000002 (77 ms later).
Subsequent routine replies retained2. Extended reply22:28:57.408 had A22/B22,
context2, with tail24..31 unchanged02580af000000af0. Thus the CC changed both
leading target words in this case without changing the limit/configuration tail.
This is one new prospective match, separate from the four retrospective cases.
It supports the manual update-ID model, not bridge receiver/application proof.
The user has not separately confirmed the CC screen after this action.

Prepared0.11 reproduced all7 genuine CC replies in the new observation exactly;
all8 frames (including the button report) passed authentication and the host
protocol suite under ASan/UBSan. No source or binary change was needed.
STATUS22:29:57 confirmed target22 C/context2, all heads disabled/no pending,
AUTOSTART OFF, MQTT connected and no queue drops. The two bridge reply counts
are historical from the earlier test; this passive observation generated no TX.

Next implementation must allocate a new manual ID once per distinct action,
preserve the other three context bytes and configuration tail, and retain the
same ID/payload on retries. A replacement command must not reuse an ID already
transmitted for another target, even if its ACK was lost. Local-button precedence
and restart recovery need the same explicit state model. Refuse byte255 overflow
until wrap behavior is known. These candidate requirements are now implemented/tested in0.12, except durable
ID reservation/automatic recovery, which remain explicitly gated. Application
behavior on the physical head is still not passed.
After those checks, a single coordinated short spare-only control window can
verify actual application and local precedence; no further CC switching now.

Current live device:0.12 saved SHADOW, all heads disabled/no pending, autoOFF;
CC ON.0.11 was never flashed; its codec/V2 fixes are included in0.12. No live
control or CC-off test is running.

Reproducible evidence:control-context-correlation.json,cc-followup-correlation.json,
manual-context-model-check.json,manual-holdout-prediction.json,
passive22-check.json,passive22-context-correlation.json,passive22-replay-result.txt,
bridge011-cc-replay-result.txt,bridge011-production-migration-result.txt,
bridge-0.11-prepared-manifest.json.
