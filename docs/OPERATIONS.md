# Operations, maintenance and recovery

For bridge 0.13.1, documented 22 October 2026. Use this with the
[firmware manual](FIRMWARE.md), [HA manual](HOME-ASSISTANT.md) and
[dated handoff](../data/COMMISSIONING-20261022.md).

## Current ownership and acceptance

At the recorded 09:22 commissioning checkpoint, CC is off. Heltec owns nodes
2,5,6,8,12,13,16 with AUTOSTART on, V3 generation 76. Deleted node 15 is disabled.
Six reachable heads ACKed before and after reset. Node 5 / inferred Small bedroom
has no takeover contact; the user deferred its low battery/placement work.

The temporary commissioning guard was explicitly stopped after the six-head
recovery check. No automatic timed SHADOW is intended to remain pending from that
trial. The deployment is not a whole-house or indefinite-operation certificate:
ID255 rollover and V3 electrical-interruption/stress acceptance remain open.

Reading or editing documentation does not require changing controller mode,
flashing, rebooting HA, restarting Mosquitto or power cycling CC.

## Physical and service inventory

| Item | Current path/address |
|---|---|
| Workstation project | `<project>` |
| Pi SSH alias | `rpi5` |
| Pi capture root | `~/danfoss-captures/bridge-20261021` |
| Serial journal | `raw.jsonl` under that root |
| Command spool | `commands/` under that root |
| User service | `danfoss-bridge-capture.service` |
| Deployed capture script | `~/danfoss-captures-tools/bridge_capture_v2.py` |
| Local source equivalent | `tools/bridge_capture.py` |
| Pi Python environment | `~/listening-post/venv/bin/python3` |
| Esptool | `~/listening-post/venv/bin/esptool.py` |
| Serial adapter | `/dev/serial/by-id/usb-Silicon_Labs_CP2102_USB_to_UART_Bridge_Controller_0001-if00-port0` |
| Flash maintenance lock | `~/listening-post/FLASHING` |
| Pi firmware/backup directory | `~/listening-post/fw/` |
| HA | `http://homeassistant:8123/` |

The date-labelled capture directory continues collecting later dates. Do not
start a new receiver simply because its name says 21 October. The unrelated
T-Embed/433.92-MHz capture is not the Heltec serial/radio controller; leave it alone.

### Pi dependency

The bridge's firmware runs its own radio and MQTT logic. Stopping the logger
does not stop it. Conversely, removing the USB lead can remove its power in this
physical installation. If moved to an independent USB supply it can operate over
Wi-Fi without serial logging, provided saved ownership/network state is valid.
Since 0.14.0 HA has what daily monitoring needs (bridge device, per-head
diagnostics, event log and alerts; see the HA manual, *Monitoring without the
Pi*). Without a USB host you lose only the full serial journal, the console
commands and flashing. Moving the board does not automatically fix uplink range;
the bridge's own Wi-Fi read -83 dBm at its install position.

## Read-only health checks

From the workstation:

```sh
ssh rpi5 'systemctl --user status danfoss-bridge-capture.service --no-pager'
ssh rpi5 'tail -n 5 ~/danfoss-captures/bridge-20261021/raw.jsonl'
ssh rpi5 'systemctl --user list-units "danfoss-commission*" --all --no-pager'
```

Queue a STATUS through the recipe below rather than opening another serial
monitor. Inspect a fresh record for expected ownership, accepted target/context,
recent reports/ACK increments, clean NVS and network status. Counts reset at boot;
a new zero count is not automatically a cumulative reliability regression.

To preserve a local evidence snapshot:

```sh
scp rpi5:~/danfoss-captures/bridge-20261021/raw.jsonl data/inspection-raw.jsonl
python3 tools/bridge_timing_report.py data/inspection-raw.jsonl
python3 tools/mqtt_probe.py --seconds 20 --output data/inspection-mqtt.json
```

For a named bounded trial, also copy its session JSON and run:

```sh
python3 tools/bridge_session_report.py data/inspection-raw.jsonl \
  --session data/commissioning-013-session.json
```

That report follows the session's specified node. It is not a seven-head coverage
report by itself. Keep per-node post-boot counters and labelled physical results.

## Single serial owner and atomic commands

The capture service is the **only normal serial owner**. Do not run `pio device
monitor`, another Python `serial.Serial`, a terminal program or esptool while it
owns the adapter. Use a maintenance stop/restart only for reset/flash work.

The deployed service was inspected as:

```ini
[Unit]
Description=Danfoss bridge serial capture and command spool

[Service]
ExecStart=~/listening-post/venv/bin/python3 ~/danfoss-captures-tools/bridge_capture_v2.py ~/danfoss-captures/bridge-20261021/raw.jsonl --commands ~/danfoss-captures/bridge-20261021/commands
Restart=on-failure
RestartSec=2
```

It is a **user** unit, not a system unit. Inspection returned `static` for
`is-enabled`; this is not evidence of automatic startup after a Pi reboot.
It has no `[Install]` section in the inspected file. User-manager lingering,
dependencies and boot startup need separate verification if reliable unattended
Pi logging is desired; do not assume `systemctl --user enable` works on this
unchanged static unit. None of this changes Heltec's saved AUTOSTART policy.

The logger opens exactly one matching CP210 adapter, disables DTR/RTS before
opening, uses pyserial exclusive mode, reconnects on errors and polls STATUS every
30 seconds. Two matching adapters require deliberate port selection rather than
opening an arbitrary one.

### Atomic STATUS recipe

This is a read-only controller command. The Python code executes on the Pi:

```sh
ssh rpi5 python3 - <<'PY'
from pathlib import Path
import time
directory = Path('~/danfoss-captures/bridge-20261021/commands')
pending = directory / ('manual-' + str(time.time_ns()) + '.tmp')
pending.write_text('STATUS\n', encoding='ascii')
pending.replace(pending.with_suffix('.cmd'))
print('STATUS queued')
PY
```

Use the same pattern for an explicitly intended maintenance command. Each file
must contain **one ASCII line**, at most 90 characters after stripping. Never
place multiple serial commands in one file. For a sequence, use separate uniquely
named files whose names sort in the intended order, and verify each prerequisite
before sending a dependent action.

The logger changes `.cmd`→`.sending` before serial write and then `.sent` after
flush. Invalid files become `.rejected`. A `.sent` file proves host transmission,
not firmware acceptance. A leftover `.sending` after a host crash is deliberately
not replayed because delivery is uncertain. Inspect logs/state; do not blindly
rename it back to `.cmd` or duplicate a control request.

The `FLASHING` lock is a maintenance convention used by our reset/flash workflow,
not an automatic interlock implemented in the capture script. Stop the capture
service and verify the port is free even when the lock exists.

## Change a target normally

Prefer HA's climate action/standard UI, using a valid half-degree value. MQTT
cannot enable a head, release SHADOW or change AUTOSTART. To inspect delivery,
compare receipt, pending state, reply ACK and physical head when required.

Serial `SET N 19.5` is an operator alternative, still subject to enabled ownership,
known context, ID budget and checkpoint-before-TX. A `set`/`queued` event does not
mean that a sleeping head has received the target. Avoid speculative configuration
imports or repeated head resets to solve ordinary schedule delays.

## Return control to the CC

Do this only when intentionally returning to the original controller:

1. Queue exact `SHADOW` through the atomic spool.
2. Require a new `mode` event with `shadow:true` and
   `recovery_disabled_saved:true`. A failed save means the reboot ownership might
   still exist; do not declare the release durable.
3. Queue/read fresh STATUS. Every head must be disabled and nonpending;
   `storage.autostart:false`, `dirty:false`, storage ready, no unresolved save error.
4. For a recovery acceptance exercise, reset and verify the passive state persists
   before restoring CC. A reset is not necessary for every routine read-only check.
5. Only after verified release, power CC and let it finish startup. Confirm room
   communication returns. Leave Heltec in saved SHADOW while CC controls.

`AUTOSTART OFF` alone does not disable current control. `CONTROL N OFF` alone
does not release all the other heads. An unpowered Heltec with old AUTOSTART-on
state can resume ownership when plugged in; verify saved release before allowing
both controllers to operate.

Stopping bridge control after manual context use can invalidate that context;
rearming may need genuine CC learning. Do not alternate OFF/ON as a casual
connectivity probe.

## Commission from passive state

This is the controlled procedure for a replacement board or future re-commission,
not a request to repeat the current user's CC switching:

1. Confirm the intended firmware, network/key, valid saved state and physical node
   identities. Fresh/legacy storage starts passive; keep it passive with CC on.
2. Observe genuine CC target/context per head. Learn extended configuration where
   available. Capture enough data to identify real devices; do not substitute a
   deleted spare or invent configuration bytes.
3. Document existing targets, coverage, room mappings and rollback. Keep the
   capture service the sole serial owner.
4. Obtain/record that CC is off before enabling any head. A passive observation
   period is compatible with CC on; active reply transmission is not.
5. For a changed protocol/storage policy, first use the correct spare 16 with a
   bounded session/guard, remote physical target check and local precedence check.
6. Enable intended heads with `CONTROL N ON`. Wait for the necessary checkpoints;
   then set `AUTOSTART ON` and require its saved confirmation/clean state.
7. Keep existing targets during multi-head takeover. Verify each head's own
   reports and matching reply ACKs. A shared room temperature does not prove both
   physical heads responded.
8. Verify saved reset recovery at unchanged ownership/targets and new per-head
   post-reset ACKs. Label reset versus physical power cut accurately.
9. Resolve or explicitly record each exception. In the current deployment the user
   deferred node 5; this remains a known unsupported room, not a passed RF gate.
10. Explicitly stop a temporary guard only when the intended active outcome and
    exceptions are recorded; otherwise let it stop and verify saved SHADOW before
    asking for CC restoration.

`AUTOSTART ON` requires at least one eligible durable enabled head. A command
acknowledgement saying “save queued” is insufficient to certify recovery. Verify
the resulting record generation, clean state and resumed behavior.

### Optional bounded trial guard

`tools/bridge_trial_guard.py` is a separate host test process. It requires a
session JSON naming spare 16, explicit CC-off confirmation and an aware deadline
no more than ten minutes ahead at startup. It is not a production watchdog or a
permanent monitor. The rollout reused a node-16 session while listing the wider
authorized nodes explicitly; the guard's actual stop always releases **all** heads.

At deadline it queues unique SHADOW and STATUS files, waits for fresh all-disabled,
nonpending, AUTOSTART-off/clean storage, then records real verification time. If
verification fails, it exits with an error; a queued command alone is not proof
of stopped radio. Stopping its unit deliberately leaves whatever controller state
currently exists. Never fake `control_disabled_verified:true` to suppress it while
the controller is active.

## Reset and power recovery

An EN/RTS reset restarts the processor while USB power remains. A physical unplug
tests power interruption. A power cut during a flash write is a stronger/different
test again. The current six-head acceptance used EN reset, not the latter two.

Before an intentional active reset:

- Require the expected nodes/targets and clean saved AUTOSTART-on checkpoint.
- Preserve a recent journal/status/session snapshot.
- Check a pending prepared intent: V3 may resume it rather than the older accepted
  target; that is designed recovery, not a stale-command bug by itself.
- Use exclusive maintenance access if esptool is needed and restore capture after.

After reset:

- Inspect new `boot`: expected firmware, `radio:true`, `recovered:true`, intended
  AUTOSTART/ownership mode.
- Confirm exact enabled list and targets before waiting for new RF traffic.
- Expect counters/measurements/ages to start empty; allow normal wakes.
- Verify each reachable head's ACK counter advances from that new boot.

Both NVS slots invalid, wrong home/key tag or unsupported format means passive
ownership. Do not force-enable a head with guessed context to hide a recovery
failure. Restore a compatible known state or re-learn passively with CC.

## Firmware build, flash and rollback

### Network key

The 16-byte Danfoss Link network key (XTEA CBC-MAC) is unique to this household's
link. It was set at pairing and is stored in every head; it cannot be changed
without re-pairing all heads. Anyone holding it within radio range can forge
authenticated commands, so it never goes into git, docs or a command line.

| Where | What |
|---|---|
| macOS login Keychain, service `danfoss-link-network-key` | The single working copy |
| Password manager (recommended) | Offline backup: without it a lost Mac means no rebuilds |
| `DANFOSS_NETWORK_KEY` env var | Optional override for one-off builds/tests elsewhere |
| Flashed Heltec, `.bin` images, NVS dumps | Compiled in; these files stay gitignored |

- **Build:** `firmware-bridge/scripts/network_key.py` (PlatformIO pre-script, also
  used by `firmware-test`) writes `danfoss_key.h` into `.pio/build/<env>/generated/`
  with mode 600. There is no `src/danfoss_key.h`. A missing key stops the build.
- **Python tools** call `tools/danfoss_secret.py` `network_key()`.
- **Commit guard:** `.githooks/pre-commit` (enabled with
  `git config core.hooksPath .githooks`) runs `danfoss_secret.py scan` on the staged
  files. It detects the key plain, bit-inverted, at any bit offset, as raw bytes,
  hex or a C array, plus the WiFi/MQTT passwords. Raw captures of a pairing contain
  the key, because the CC sends it in clear (op `80` sub `04`).
- **New captures:** `python3 tools/danfoss_secret.py redact FILE...` blanks the
  `hex` of capture records that carry the key and replaces aligned text with
  `<network-key>`.
- **Restore on a new Mac:** `security add-generic-password -a ffd2bb2d -s
  danfoss-link-network-key -w`. With no value after `-w`, `security` prompts for
  it, so the key stays out of shell history.
  Then `python3 tools/danfoss_secret.py check`.

### Audited partition layout

Decoded from the current local generated `partitions.bin`:

| Partition | Offset | Size |
|---|---|---|
| nvs | `0x9000` | `0x5000` |
| otadata | `0xe000` | `0x2000` |
| app0 | `0x10000` | `0x330000` |
| app1 | `0x340000` | `0x330000` |
| spiffs | `0x670000` | `0x180000` |
| coredump | `0x7f0000` | `0x10000` |

These offsets apply to the audited existing 8-MiB layout. Verify the actual target
and partition/boot selection before reusing an address. The presence of app1 does
not mean an OTA firmware update service has been implemented.

### Prepare a new release

1. Make the intended surgical source change. Update version consistently in boot
   event/OLED/source and versioned documentation.
2. Run the relevant host suites and build using the private network header.
3. Record resolved libraries and SHA-256 of the exact application binary. Store a
   new release manifest instead of altering the old release's recorded hash.
4. Preserve rollback image and NVS before touching hardware. Firmware/NVS backups
   are private because they may contain credentials/network state.
5. Decide whether the update preserves schema/ownership or needs passive migration;
   never assume an arbitrary older image understands a current active V3 record.

### Update over Wi-Fi (0.15.0 and later)

The bridge needs no USB host for updates:

```sh
pio run -d firmware-bridge -e heltec_v3          # build
python3 tools/ota_update.py                      # push the build to danfoss-bridge.local
```

The OTA password is `NET_OTA_PASSWORD` in the private network header (generated by
`tools/configure_bridge_network.py`, stored in `data/heltec-network.private.json`).
The new image is written to the other app slot and boots on trial. It must reach
the MQTT broker within 5 minutes, or it falls back to the previous slot by itself
(also after three crashing boots). `sensor.danfoss_heltec_last_event` shows
`ota_confirmed` on success; the bridge state's `partition` names the running slot.
First OTA, 22 Oct 2026 22:13: written, booted app1, state recovered, confirmed in 8 s.

**USB after an OTA:** the boot slot may now be `app1`, so an app-only
`write_flash 0x10000` would be ignored at boot. Use a full PlatformIO upload, or
erase `otadata` (`erase_region 0xe000 0x2000`) together with the `0x10000` write.

### Exclusive maintenance procedure

Current Pi tool syntax accepts these esptool subcommands (with possible deprecation
warnings for underscore spellings). This is a procedure reference, not a request
to execute it against the running controller now:

1. Create `~/listening-post/FLASHING` exclusively; fail if it already exists.
2. Stop `systemctl --user stop danfoss-bridge-capture.service` on Pi.
3. Run `fuser` against the exact serial path and require no owner before esptool.
4. Back up existing NVS using `read_flash 0x9000 0x5000 <unique-private-backup>`.
   Protect backup files with mode 0600. Keep any required OTA/partition backups.
5. For a compatible **application-only update of this installed layout**, use
   `write_flash 0x10000 <verified-application-image>` with chip `esp32s3` and the
   explicit port. This leaves NVS/bootloader/partition table untouched.
6. Require esptool's digest verification and a matching new boot event. Do not
   call a successful command dispatch a verified flash.
7. In a guaranteed cleanup/finally path, remove the maintenance lock and restart
   the capture service, including when the operation fails.
8. Check new STATUS, saved compatibility/ownership and resumed RF/MQTT behavior.

Keep file transfer, hash verification, backup and flash steps sequential. Do not
start another serial monitor to “check progress.” If a maintenance lock remains
after a crash, inspect the owner/process state before removing it.

For a **blank or differently partitioned board**, application-only `0x10000`
writing is not a complete install. Use the correct PlatformIO full upload workflow
for that environment and hardware, including its generated bootloader/partition
artifacts, after arranging exclusive access. Do not invent generic ESP32 offsets
for a differently configured target. `pio run -d firmware-bridge -e heltec_v3 -t
upload --upload-port <port>` is a full-upload tool invocation, not a substitute
for backup/schema/ownership planning on the deployed controller.

### Available rollback material

See [0.13.1 manifest](../data/bridge-0.13.1-manifest.json) for exact hashes/paths.
Recorded Pi artifacts include:

- `~/listening-post/fw/bridge-0.13.1.bin` — current image.
- `~/listening-post/fw/bridge-0.13.bin` — prior image without room metadata.
- `~/listening-post/fw/bridge-0.12.bin` — older passive-boot candidate.
- `~/listening-post/fw/nvs-before-0.13.1-20261022.bin` — V3 spare-active
  checkpoint before metadata update; not the later seven-head generation 76.
- `~/listening-post/fw/nvs-before-0.13-20261022.bin` — earlier V2 passive
  backup; schema/ownership implications differ from current V3.

Restoring an old NVS backup restores its old ownership/targets as well as bytes.
Do not blindly write a spare-active backup while CC is powered, or assume a
pre-update backup contains the later household rollout. A compatible older image
plus incompatible saved state may boot passive rather than resume control.
Prefer a deliberate saved-SHADOW rollback when handing control back to CC.

## Common incidents

### Head alarms while the other rooms work

Check that head's own reports/ACKs and battery. A saved enabled flag is not contact.
Wake with the middle button; compare location/RSSI and battery status. Improve
placement and replace low batteries before changing protocol/configuration.
Node 5 historically had only rare packets around −103 to −109 dBm. Other rooms'
success does not prove its RF path. A large external antenna is not evidence of
adequate coverage in every location.

### Lock icon or target limit

Identify the exact physical head and whether it is still paired/in use on CC.
The project previously mixed deleted spare 15 with working spare 16. Save raw
frames/configuration and distinguish display check from pressing up/down. Do not
overwrite opaque configuration bytes or assume a 19 °C target means a 19 °C max
limit. Record the actual observation and whether a battery restart changed it.

### Storage error / target never leaves pending

Inspect `save_failures`, `saved`/`err`, current context and pending ages. Failed
durability intentionally blocks application TX. ID255 can also reject a new
target; recovery requires a validated policy, not resetting counters arbitrarily.
If returning to CC, follow verified saved SHADOW rather than assuming a failed
AUTOSTART/STOP command was durable.

### Broker/HA loss

Check the network/integration without switching CC on. Radio can continue at its
last target while MQTT is down. Restore broker connectivity, use a distinct probe
client ID, and inspect retained/discovery/state on reconnection. Reconcile any HA
schedule missed during the outage according to an intentional override policy.

### Lost logger / Pi reboot

Check user service, adapter path, power and `.sending` files. Logging failure is
not proof that Heltec control stopped. Conversely, restoring Pi power may reboot
the USB-powered bridge with saved AUTOSTART on. Confirm ownership before restoring
CC. Verify future Pi boot startup explicitly rather than relying on the static
service's presence.

## Handoff checklist

Record actual firmware/hash, CC state, enabled IDs, AUTOSTART and generation,
per-node last-seen/ACKs, physical checks, coverage exceptions, active guard state,
backup/rollback paths and remaining gates. Keep private values out of the report.
Use explicit “reset,” “physical power cut,” “link ACK” and “display applied” terms.
The current known exceptions are node 5, ID255 rollover and V3 interrupted-write
acceptance; documentation completion does not silently turn those into passes.
