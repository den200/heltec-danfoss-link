#!/usr/bin/env python3
"""Generate the private network build header; never print credential values."""
import json
import os
import secrets
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def main():
    source = ROOT / 'data/heltec-network.private.json'
    config = json.loads(source.read_text())
    if not config.get('ota_password'):
        # Firmware updates over Wi-Fi need a password; create one on first use.
        config['ota_password'] = secrets.token_urlsafe(18)
        source.write_text(json.dumps(config, indent=2) + '\n')
    fields = {'wifi_ssid': 'NET_SSID', 'wifi_password': 'NET_PASSWORD',
              'mqtt_host': 'NET_MQTT_HOST', 'mqtt_username': 'NET_MQTT_USER',
              'mqtt_password': 'NET_MQTT_PASSWORD', 'ota_password': 'NET_OTA_PASSWORD'}
    missing = [name for name in fields if not isinstance(config.get(name), str) or not config[name]]
    if missing:
        raise SystemExit('Fill private configuration fields: ' + ', '.join(missing))
    for name in fields:
        if any(ord(c) < 32 for c in config[name]):
            raise SystemExit('Control characters are unsupported in ' + name)
    if len(config['wifi_ssid'].encode()) > 32 or len(config['wifi_password'].encode()) > 63:
        raise SystemExit('Wi-Fi SSID/password exceeds supported byte length')
    port = config.get('mqtt_port', 1883)
    if type(port) is not int or not 1 <= port <= 65535:
        raise SystemExit('mqtt_port must be an integer from 1 to 65535')
    lines = ['#pragma once', '// Generated private credentials. Do not commit or log.']
    for name, variable in fields.items():
        lines.append(f'static const char *{variable} = {json.dumps(config[name], ensure_ascii=False)};')
    lines.append(f'static const uint16_t NET_MQTT_PORT = {port};')
    target = ROOT / 'firmware-bridge/src/network_config.private.h'
    temp = target.with_suffix('.tmp')
    fd = os.open(temp, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    os.fchmod(fd, 0o600)
    with os.fdopen(fd, 'w') as f:
        f.write('\n'.join(lines) + '\n')
    temp.replace(target)
    os.chmod(source, 0o600)
    print('Private network build header written.')


if __name__ == '__main__':
    main()
