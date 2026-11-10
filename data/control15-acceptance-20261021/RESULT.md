# Spare-head radio trial result — 21 October 2026

**Radio acceptance gate passed. Final visual observation was unavailable.**

- Bridge 0.4 unchanged; node 15 only; CC off per user confirmation.
- First reply 11:39:56.285, verified SHADOW at 12:56:34.410 Europe/Copenhagen:
  76.64 minutes, 26 reports/replies and 26 matching head ACKs.
- Zero transmit API failures and no captured reboot. Largest routine report gap
  4.84 minutes. Target remains 17 C after the user's local-button change.
- All 52 captured report/reply data frames independently pass Python MAC checks.
  Head application sequence and bridge radio sequence wrap are both observed.
- User visually confirmed remote 18 C application, then local 17 C persistence
  without warning earlier in the run. Final end-of-run warning/display observation
  is still pending; do not claim it was watched throughout the hour.
- SHADOW acknowledged at 12:56:34.305; subsequent STATUS verifies EVERY head
  disabled. User has been asked to restore CC power and confirm the final display.
- The bench-check automation is PAUSED. Requested five-minute checkpoints were
  stored by the app as a 30-minute recurrence; actual trial ran longer than the
  planned hour. Do not report an exactly timed one-hour cutoff.

The 0.5 persistence build is local only, not flashed. Next: confirm CC restoration,
then plan/run its separate power-cycle and persistence timing bench before MQTT/HA.

## User follow-up — about 14:18

User returned much later and reported a warning on the spare after the bridge had
been silent since 12:56. This does not establish a warning during the accepted
radio run; do not claim a final visual check occurred. Earlier local-button/no-
warning confirmation and the 26 matching ACKs remain the evidence. The user has
restored CC power and says boot takes time. As of 14:18, bridge STATUS confirms
all heads disabled, zero bridge transmissions since shutdown, and no captured CC
reply yet. RF recovery is pending; the spare itself continues authenticated reports.
