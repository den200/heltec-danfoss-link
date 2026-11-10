#!/usr/bin/env python3
"""Update the bridge firmware over Wi-Fi (no USB needed).

  ota_update.py [FIRMWARE.bin] [--host danfoss-bridge.local]

Defaults to the last PlatformIO build. The OTA password comes from the private
network header and is never printed. The new image runs on trial: if it does not
reach MQTT within 5 minutes (or crashes three times) the bridge boots the previous
image again. Watch sensor.danfoss_heltec_last_event for `ota_confirmed`.
"""
import argparse, hashlib, re, subprocess, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ESPOTA = Path.home() / '.platformio/packages/framework-arduinoespressif32/tools/espota.py'


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('firmware', nargs='?', default=ROOT / 'firmware-bridge/.pio/build/heltec_v3/firmware.bin', type=Path)
    ap.add_argument('--host', default='danfoss-bridge.local')
    a = ap.parse_args()
    header = (ROOT / 'firmware-bridge/src/network_config.private.h').read_text()
    m = re.search(r'NET_OTA_PASSWORD\s*=\s*"([^"]+)"', header)
    if not m:
        sys.exit('NET_OTA_PASSWORD missing: run tools/configure_bridge_network.py and reflash once over USB')
    data = a.firmware.read_bytes()
    print(f'{a.firmware} {len(data)} bytes sha256 {hashlib.sha256(data).hexdigest()} -> {a.host}')
    r = subprocess.run([sys.executable, str(ESPOTA), '-i', a.host, '-p', '3232', '-a', m.group(1),
                        '-f', str(a.firmware), '-r'])
    sys.exit(r.returncode)


if __name__ == '__main__':
    main()
