# Getting started on your own Danfoss Link system

This takes an existing Danfoss Link installation (a Link CC with paired radiator
heads) and moves the CC's radio job to a Heltec WiFi LoRa 32 V3, controlled from
Home Assistant over MQTT. The heads keep regulating their own valves.

It has run in one house (six heads, a Link CC Mk IV) since October 2026. Treat
your installation as a test until it has run for a while: keep the CC so you can
go back to it.

## What you need

- Heltec WiFi LoRa 32 V3, the 863–928 MHz version, with an 868 MHz antenna.
- Your Danfoss Link CC, working, with its heads paired. It is needed once to
  learn the network key and while the bridge learns each head's state.
- Home Assistant with the Mosquitto broker add-on and a dedicated MQTT user.
- PlatformIO and Python 3. On macOS the key is kept in the Keychain; on other
  systems the build reads it from the `DANFOSS_NETWORK_KEY` environment variable.

## 1. Capture your Home ID and network key

Every Link network has a 4-byte Home ID, sent in clear in every frame, and a
16-byte key that signs every command. The CC sends the key to a head, unencrypted,
when it pairs it. The capture firmware records that.

```sh
pio run -d firmware-bridge -e capture -t upload
mkdir -p private
pio device monitor -d firmware-bridge -e capture | tee private/capture.log
```

The capture firmware only receives. Leave it running for ten minutes: heads report
every few minutes, so the Home ID and node numbers show up. Then pair a head while
it keeps running, with the Heltec within a few metres of the CC: either add a new
head, or remove one head from the CC and add it again, following Danfoss's
installation guide. A re-added head gets a new node number and needs its room
assigned again in the CC. Let it capture a few more minutes, then stop it.

```sh
python3 tools/extract_key.py private/capture.log           # report only
python3 tools/extract_key.py private/capture.log --store   # save the key in the Keychain
```

The tool prints the Home ID, the node numbers it heard and whether the key
verifies against the signed frames in the capture. Outside macOS use `--print`
and put the value in `DANFOSS_NETWORK_KEY`. `private/` is gitignored; the log
holds your key, so keep it there or delete it. `python3 tools/extract_key.py
--port /dev/cu.usbserial-XXXX` reads the Heltec directly if pyserial is installed.

To find which node is which head, press the button on a head (or turn it) and
see which `src` reports in the capture straight away.

## 2. Configure

```sh
cp firmware-bridge/src/household.example.h firmware-bridge/src/household.private.h
cp data/heltec-network.example.json data/heltec-network.private.json
```

- `household.private.h`: your Home ID and one `X(node, "room")` line per head.
  The Home ID becomes part of the MQTT topics and HA unique IDs.
- `heltec-network.private.json`: Wi-Fi, the broker address and the MQTT login.
  Then run `python3 tools/configure_bridge_network.py`, which writes
  `firmware-bridge/src/network_config.private.h` and creates a password for
  firmware updates over Wi-Fi.
- `git config core.hooksPath .githooks` enables the commit hook that refuses
  any file holding your key or passwords.

The documentation uses the author's Home ID, `ffd2bb2d`, in topic and entity
examples. Read yours in its place.

## 3. Build and flash the bridge

```sh
python3 tools/test_extract_key.py      # optional host checks, see docs/FIRMWARE.md
pio run -d firmware-bridge -e heltec_v3 -t upload
```

A fresh bridge starts in SHADOW: it listens and publishes what it hears, but
never transmits. Your heads appear in Home Assistant through MQTT discovery.

## 4. Take over from the CC

Follow [Commission from passive state](OPERATIONS.md#commission-from-passive-state).
In short:

1. With the CC still on, leave the bridge in SHADOW until it has seen the CC
   answer every head. It learns each head's target and the counter the CC uses.
2. Switch the CC off.
3. On the bridge's USB serial (115200 baud), send `CONTROL <node> ON` for each
   head, check that each head acknowledges, then send `AUTOSTART ON` so control
   survives a restart.

From then on, set temperatures from the HA thermostats. A change reaches a head
when it next wakes up, usually within a few minutes; pressing the head delivers
it at once.

To go back to the CC, follow
[Return control to the CC](OPERATIONS.md#return-control-to-the-cc). Never run the
CC and an active bridge at the same time.

## 5. Optional: rooms, schedules and dashboard in Home Assistant

`ha/heating_setup.py` builds the author's setup: room helpers, schedule groups,
away mode, alerts and a dashboard. It reads `ha/household.private.json` (copy
`ha/household.example.json`) and a long-lived token from `~/.ha_token`. It
assumes rooms and entities that match the author's house, so read it before
running it with `--dry-run`. See [the Home Assistant manual](HOME-ASSISTANT.md).

## Known limits

- The bridge cannot pair heads. Pairing still needs the CC.
- Every target change advances a per-head update counter. When its low byte
  reaches 255 the bridge refuses further changes for that head instead of
  guessing how the counter rolls over; HA shows the updates left. See
  [Lock icon or target limit](OPERATIONS.md#lock-icon-or-target-limit).
- 868.40 MHz only (European Link). Transmit power is 14 dBm, the EU SRD limit.
- Only Danfoss Link radiator heads have been tested; other Link devices
  (floor heating controllers, relays, room sensors) are not handled.
