# Real NVS fallback bench — 21 October 2026

Storage-only firmware `nvs-bench-1` passed **241 fault cases** on the Heltec.
It has no radio initialization and explicitly uses namespace `dfbench`, never
`danfoss`. It tests a maximum-size 120-byte record: 119 nonempty truncations,
one missing latest record, 120 single-byte corruptions and both copies invalid.
A freshly opened store must recover the prior valid generation/target for every
invalid latest record. Two invalid records must return no recoverable state.
Restoring a valid newest copy must recover the newer target. The test namespace
is cleared before reporting success.

This injects invalid application records through real Preferences/NVS commits.
It does **not** interrupt electrical power during an in-flight flash operation.

Before testing, NVS was backed up privately in `before-nvs-bench-20261021.bin`.
After testing the verified 0.5 image was restored; generation 10, saved targets,
SHADOW and AUTOSTART OFF survived. Bridge 0.6 diagnostics were subsequently
flashed/verified in SHADOW. CC remained on throughout. Evidence: nvs-bench-result.json,
nvs-bench-before.json, nvs-bench-restored.json and both bench flash logs.

Build explicitly with `pio run -d firmware-bridge -e nvs_bench`. Default PlatformIO
build/upload selects `heltec_v3`, never the storage bench. Deploy only the app at
0x10000, with the sole capture service stopped and FLASHING lock held; restore a
verified bridge app afterward. Do not use this bench as normal controller firmware.

# Timing diagnostics in 0.6

Saved events expose start_ms and duration_us for actual write/readback; unchanged
saves have zero duration. STATUS exposes last/max save time, failed write count,
dropped log-event count and maximum ISR-to-radio-task delay. Event ms now records
queue time; queue_delay_ms measures delayed logging. These counters reset on boot.
The ISR measure excludes hardware interrupt latency, and queued events follow
radio handling rather than timestamping packet arrival. Zero queue drops does
not prove that no RF packets were missed. No active RF/write gate is passed yet.

Run `python3 tools/bridge_timing_report.py JOURNAL` for the latest boot's summary.
