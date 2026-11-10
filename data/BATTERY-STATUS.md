# Battery investigation — 21 October 2026

## Finding

The user identified the low-battery living-room head by changing its local target
to 17 C. At **11:00:54.706 Europe/Copenhagen**, node **6** sent an authenticated
53-byte button report with target `06a4` (1700). The CC replied with the same target
at 11:00:55.707. This labels node 6; the second living-room head is not yet mapped.

Before battery replacement, node 6's raw final status word was consistently
`fe00`. After the user installed fresh batteries, three authenticated reports at
**11:07:39.127**, **11:07:59.388** and **11:08:02.761** carried **`4f00`**.
Other captured heads differ:

| Node | Raw status | Distinct reports in current analysis |
|---|---|---|
| 2 | `4000`, `4600` | 44 |
| 6 (user-labelled low battery) | `4e00`, `4f00`, `fe00` | 77 |
| 8 | `3c00` | 58 |
| 12 | `3400` | 23 |
| 13 (likely master bedroom) | `4100` | 2 |
| 16 | `5200`, `5300` | 9 |

The user confirmed that the CC low-battery warning cleared after replacement.
Together with the authenticated field change, this confirms **`fe00` as the
observed low-battery indication on node 6**.
Do not interpret the other values as percentages or voltage, or assume byte order,
thresholds, or the meaning of the second byte. Node 5 has no captured head report
in this analysis, so its battery state cannot be assessed.

## Location and layouts

On authenticated direct head reports (op=10, application prefix `1x 0c`):

- Routine 57-byte layout (`47` at offset 17): raw word at offsets **50–51**.
- Alternate 57-byte layout (`85` at offset 17): same offsets **50–51**;
  its temperature-like data does **not** share the routine report's offsets.
- Button 53-byte layout (`46` at offset 17): raw word at offsets **44–45**;
  locally selected target at **46–47**.

Offsets are zero-based from the complete Home-ID-prefixed frame. Check the full
observed descriptor prefix before using these offsets. In particular, frame length
alone does not identify a layout. Treat unknown layouts as raw observations.

## Evidence and reproduction

- `battery-capture-20261021.jsonl`: read-only export of all zw-tagged Heltec/T-Embed
  raw windows available in the Pi SQLite database at about 10:59 local time.
  Each row retains SQLite rowid, timestamp and original serial line.
- `bridge-shadow-20261021.jsonl`: snapshot of the running bridge serial journal,
  including the labelled button change. Original remains on the Pi at
  `~/danfoss-captures/bridge-20261021/raw.jsonl`.
- `head-status-analysis-20261021.json`: machine-readable comparison, deduplicated
  by full frame per node, retaining button timestamps, frames and source row numbers.
  Occurrence counts can overlap between receivers; they are not wake-cycle counts.
- The analysis authenticates frames with the recovered network key. Two malformed
  serial records are skipped and counted, not silently treated as valid reports.

Reproduce from the project directory:

```sh
python3 tools/analyze_head_status.py data/battery-capture-20261021.jsonl \
  data/bridge-shadow-20261021.jsonl > data/head-status-analysis-20261021.json
```

## Next check

Fresh batteries are installed and the field transition is captured in
`battery-replacement-20261021.json`, including exact authenticated before/after
frames and source rows. The user confirmed the CC low-battery indicator cleared.
The user then replaced the master-bedroom batteries and confirmed its CC warning
cleared; the head had appeared completely off. Node **13** first appears at
**11:12:00.615**, followed by authenticated alternate and routine reports carrying
**`4100`**. This timing makes node 13 the likely master-bedroom head; the room
mapping is inferred, not proven by a labelled button change. No node-13 report
exists before replacement in the retained captures, so this head cannot provide
an independent low-to-healthy field transition. Its earlier silence could reflect
exhausted batteries or reception gaps; the user's observation supports the former.
Exact frames and timestamps are in `master-bedroom-battery-20261021.json`.

The CC uses op=`50` for node 13's target replies (16 C), plus additional op=`10`
configuration messages. Local bridge shadow learning now accepts the observed
op=`50` target layout. Active control of this head, including its extra configuration
exchange, remains untested; do not infer support from successful shadow decoding.
Keep the real CC on and the bridge in SHADOW. Do not expose a battery percentage;
`4f` is an observed raw value, not evidence of 79% charge.
