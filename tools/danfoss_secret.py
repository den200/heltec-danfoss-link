#!/usr/bin/env python3
"""Single access point for the Danfoss Link network key and for keeping it out of git.

The key lives only in the macOS login Keychain (service danfoss-link-network-key) or,
for one-off runs elsewhere, in the DANFOSS_NETWORK_KEY environment variable (32 hex).
Never write it to a project file, a command line or a log.

  danfoss_secret.py scan FILE...   exit 1 if any file holds the key or a WiFi/MQTT password
  danfoss_secret.py redact FILE... replace the key in place (text and JSONL captures)
  danfoss_secret.py check          confirm the key is retrievable (prints no key material)

Detection covers plain, bit-inverted (radio-wire form) and every bit offset, as hex text
or raw bytes. Pairing frames (op 80 sub 04) carry the key in clear, so raw captures of an
inclusion contain it shifted by the capture's bit phase.
"""
import json, os, re, subprocess, sys
from pathlib import Path

SERVICE = 'danfoss-link-network-key'
ROOT = Path(__file__).resolve().parents[1]
PLACEHOLDER = '<network-key>'


def network_key():
    value = os.environ.get('DANFOSS_NETWORK_KEY', '').strip()
    if not value:
        r = subprocess.run(['security', 'find-generic-password', '-s', SERVICE, '-w'],
                           capture_output=True, text=True)
        if r.returncode:
            sys.exit(f'network key unavailable: Keychain item {SERVICE!r} not found '
                     'and DANFOSS_NETWORK_KEY unset')
        value = r.stdout.strip()
    if not re.fullmatch(r'[0-9a-fA-F]{32}', value):
        sys.exit('network key must be 32 hex characters')
    return bytes.fromhex(value)


def _bit_patterns(key):
    """Byte patterns (full bytes only) for the key at each bit offset, plain and inverted."""
    out = []
    for data in (key, bytes(b ^ 255 for b in key)):
        bits = ''.join(f'{b:08b}' for b in data)
        for shift in range(8):
            s = '0' * shift + bits
            whole = [int(s[i:i + 8], 2) for i in range(0, len(s) - 7, 8)]
            out.append(bytes(whole[1:-1] if shift else whole))  # drop partial edge bytes
    return out


def _passwords():
    config = ROOT / 'firmware-bridge/src/network_config.private.h'
    if not config.exists():
        return []
    values = dict(re.findall(r'(\w+)\s*=\s*"([^"]*)"', config.read_text()))
    return [values[k].encode() for k in ('NET_PASSWORD', 'NET_MQTT_PASSWORD', 'NET_OTA_PASSWORD') if values.get(k)]


def _normalised_hex(lower):
    """Hex text from C byte arrays (0x12,0xc) and separated bytes (12 34:56-78)."""
    c_array = b''.join(b'%02x' % int(t, 16) for t in re.findall(rb'0x([0-9a-f]{1,2})\b', lower))
    separated = b'|'.join(re.sub(rb'[ :,\-]', b'', m)
                          for m in re.findall(rb'[0-9a-f]{2}(?:[ :,\-][0-9a-f]{2}){7,}', lower))
    return c_array + b'|' + separated


def findings(data, key):
    lower = data.lower()
    lower += b'|' + _normalised_hex(lower)
    found = []
    for p in _bit_patterns(key):
        if p in data or p.hex().encode() in lower:
            found.append('network key')
            break
    if any(p in data for p in _passwords()):
        found.append('WiFi/MQTT password')
    return found


def _redact_text(text, key):
    for p in _bit_patterns(key)[:1] + _bit_patterns(key)[8:9]:  # aligned plain / inverted
        text = re.sub(p.hex(), PLACEHOLDER, text, flags=re.I)
    return text


def redact(path, key):
    """Aligned key text becomes a placeholder; capture records still holding it lose their hex."""
    lines, changed = path.read_text().splitlines(keepends=True), 0
    for i, line in enumerate(lines):
        if not findings(line.encode(), key):
            continue
        if path.suffix == '.jsonl':
            record = json.loads(line)
            capture = record.get('capture') if isinstance(record, dict) else None
            nested = isinstance(record, dict) and isinstance(record.get('line'), str)
            if nested:  # database export: the capture is a JSON string in "line"
                capture = json.loads(record['line'])
            if not (isinstance(capture, dict) and 'hex' in capture):
                sys.exit(f'{path}:{i + 1}: key outside a capture record; redact by hand')
            capture['hex'] = ''
            capture['redacted'] = 'pairing frame carries the network key'
            if nested:
                record['line'] = json.dumps(capture, separators=(',', ':'))
            new = json.dumps(record) + '\n'
        else:
            new = _redact_text(line, key)
        if findings(new.encode(), key):
            sys.exit(f'{path}:{i + 1}: key still present after redaction; redact by hand')
        if new != line:
            lines[i], changed = new, changed + 1
    if changed:
        path.write_text(''.join(lines))
    return changed


def main(argv):
    if len(argv) < 2 or argv[1] not in ('scan', 'redact', 'check'):
        sys.exit(__doc__)
    key = network_key()
    if argv[1] == 'check':
        print('network key available (16 bytes)')
        return 0
    bad = 0
    for name in argv[2:]:
        path = Path(name)
        if not path.is_file():
            continue
        if argv[1] == 'scan':
            for what in findings(path.read_bytes(), key):
                print(f'{name}: contains {what}', file=sys.stderr)
                bad = 1
        else:
            print(f'{name}: {redact(path, key)} lines redacted')
    return bad


if __name__ == '__main__':
    sys.exit(main(sys.argv))
