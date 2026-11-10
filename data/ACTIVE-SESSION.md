# Pairing session prepared 2026-10-20 23:44 CEST

Pi: `ssh rpi5`. Unit: `danfoss-pairing.service` (user systemd).
Receive-only session runs 23:44:01–00:44:01 Europe/Copenhagen, across midnight.
Raw journal: `~/danfoss-captures/20261020-234401/raw.jsonl`.
Metadata: same directory, `session.json`.
Raw journal retains SQLite rowid, timestamp and exact original JSON line for every
new raw record, without Home-ID filtering. Primary SQLite logger remains running.

Both radio window acknowledgements verified at 23:44:01:
- Heltec: zw96pair, 868.42 MHz, 19.2 kchip/s (9.6 kbps Manchester), 200-byte windows.
- T-Embed: zw40pair, 868.40 MHz, 40 kbps, 64-byte windows.
No R1 radio packet received yet at readiness check; mode acknowledgement only.
T-Embed raw reception and independent journal growth verified.

The recorder pauses wide and mt timers; its finally block sends STOP to both radios
and restores those previously active timers on normal expiry or SIGTERM.
Early stop: `ssh rpi5 'systemctl --user stop danfoss-pairing'`.
Status: `ssh rpi5 'systemctl --user status danfoss-pairing --no-pager'`.
Decode: `ssh rpi5 'python3 ~/danfoss-captures-tools/zwdecode.py ~/listening-post/lp.db 10'`.

Decoder now keeps all aligned frames, accepts sync-delimited unknown/zero Home IDs,
and retains multiple Manchester runs. Raw capture is independent of decoding.
Validation: recovered all 93 prior audit occurrences; synthetic zero Home ID, multiple
NRZ frames and multiple Manchester runs passed. Sync recovery remains best effort.

User procedure: one spare head, start pairing only after ready message; report CC
success/failure and retries. No unrelated removals. After pairing leave head connected
for baseline reports, then make one timestamped setpoint change at a time.
