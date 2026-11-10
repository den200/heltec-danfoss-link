#!/usr/bin/env python3
"""Find a Danfoss Link network's Home ID, nodes and network key in a radio capture.

The CC sends the 16-byte network key in clear when it pairs a head (proprietary
frame 91 0002 80 04 <key>). Capture a pairing with the capture firmware
(pio run -d firmware-bridge -e capture -t upload) and give the log to this tool;
see docs/GETTING-STARTED.md. The key is checked against every authenticated frame
in the capture.

  extract_key.py LOG...             report Home ID, nodes and whether a key was found
  extract_key.py --port DEV         read live from the capture firmware (needs pyserial), Ctrl-C to stop
  extract_key.py LOG --store        save the key in the macOS Keychain for the build
  extract_key.py LOG --check        compare with the key already in the Keychain
  extract_key.py LOG --print        print the key (for builds outside macOS)

Input lines may be capture-firmware JSON, bridge or listening-post JSON, or bare
hex; aligned, inverted and bit-shifted windows are all decoded. A capture log
holding a pairing contains the key: keep it out of git (private/ is ignored).
"""
import argparse, collections, re, subprocess, sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from danfoss_auth import tag  # noqa: E402
from zwdecode import frames  # noqa: E402

SERVICE = 'danfoss-link-network-key'
HEX = re.compile(r'[0-9a-fA-F]{20,}')
PAIRING = bytes.fromhex('91000280 04'.replace(' ', ''))


def decoded_frames(lines):
    """Unique checksum-valid frames from every hex window in the lines."""
    seen = set()
    for line in lines:
        for blob in HEX.findall(line):
            if len(blob) % 2:
                blob = blob[:-1]
            for f, _ in frames(bytes.fromhex(blob)):
                if f not in seen:
                    seen.add(f)
                    yield f


def authenticated(f):
    """Direct singlecast Danfoss frame carrying a 4-byte tag (ops 10/50/90)."""
    n = len(f)
    return (20 <= n <= 64 and f[5] & 0x8f == 1 and f[9:12] == b'\x91\x00\x02'
            and f[12] in (0x10, 0x50, 0x90) and f[13] == n - 17)


def verifies(f, key):
    n = len(f)
    return tag(f[12:n - 5], key) == f[n - 5:n - 1]


def analyse(lines):
    homes = collections.defaultdict(lambda: {'nodes': set(), 'frames': 0, 'tagged': [], 'keys': set()})
    for f in decoded_frames(lines):
        h = homes[f[:4].hex()]
        h['frames'] += 1
        if f[5] & 0x0f != 3:  # an ACK's source is not a sender of its own traffic
            h['nodes'].add(f[4])
        payload = f[9:-1]
        if f[5] & 0x80 == 0 and payload.startswith(PAIRING) and len(payload) >= 21:
            h['keys'].add(bytes(payload[5:21]))
        if authenticated(f):
            h['tagged'].append(f)
    return homes


def keychain_key():
    r = subprocess.run(['security', 'find-generic-password', '-s', SERVICE, '-w'],
                       capture_output=True, text=True)
    value = r.stdout.strip()
    return bytes.fromhex(value) if r.returncode == 0 and re.fullmatch(r'[0-9a-fA-F]{32}', value) else None


def store(home, key):
    # security -i reads the command from stdin, so the key never appears in argv.
    command = f'add-generic-password -U -a {home} -s {SERVICE} -w {key.hex()}\n'
    r = subprocess.run(['security', '-i'], input=command, capture_output=True, text=True)
    return r.returncode == 0 and keychain_key() == key


def read_port(port):
    import serial  # pyserial
    print(f'reading {port}; pair or re-pair a head now, Ctrl-C when done', file=sys.stderr)
    lines = []
    try:
        with serial.Serial(port, 115200, timeout=1) as s:
            while True:
                line = s.readline().decode(errors='replace')
                if '"t":"frame"' in line:
                    lines.append(line)
                    print(f'\r{len(lines)} frames', end='', file=sys.stderr)
    except KeyboardInterrupt:
        print(file=sys.stderr)
    return lines


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('logs', nargs='*', help='capture logs (- for stdin)')
    ap.add_argument('--port', help='serial port of the capture firmware')
    ap.add_argument('--home', help='Home ID to use when several networks were heard')
    action = ap.add_mutually_exclusive_group()
    action.add_argument('--store', action='store_true', help='save the key in the macOS Keychain')
    action.add_argument('--check', action='store_true', help='compare with the Keychain key')
    action.add_argument('--print', action='store_true', help='print the key')
    ap.add_argument('--replace', action='store_true', help='with --store: overwrite a different stored key')
    a = ap.parse_args()
    if not a.logs and not a.port:
        ap.error('give capture logs or --port')

    lines = read_port(a.port) if a.port else []
    for name in a.logs:
        lines += (sys.stdin if name == '-' else open(name, errors='replace')).read().splitlines()
    homes = analyse(lines)
    if not homes:
        sys.exit('no valid Danfoss Link frames found')

    # A bit-shifted noise window passes the XOR checksum one time in 256. A real
    # Link network shows authenticated traffic, a pairing or steady frames.
    noise = 0
    for home, h in sorted(homes.items(), key=lambda x: -x[1]['frames']):
        if not (h['tagged'] or h['keys'] or h['frames'] >= 10 or home == a.home):
            noise += 1
            continue
        print(f"Home ID {home}: {h['frames']} frames, nodes {sorted(h['nodes'])}, "
              f"{len(h['tagged'])} authenticated, pairing key frames: {len(h['keys'])}")
    if noise:
        print(f'({noise} sporadic decodes ignored as noise)')
    home = a.home or max(homes, key=lambda k: homes[k]['frames'])
    h = homes.get(home)
    if h is None:
        sys.exit(f'Home ID {home} not in the capture')
    if not h['keys']:
        print(f'No pairing frame for {home}. Capture again while the CC pairs a head (see docs/GETTING-STARTED.md).')
        return 1
    results = []
    for key in h['keys']:
        ok = sum(verifies(f, key) for f in h['tagged'])
        results.append((ok, key))
    ok, key = max(results)
    total = len(h['tagged'])
    # A wrong key matches a 4-byte tag once in 2^32 frames; a few failures are
    # corrupted receptions that still passed the 8-bit checksum.
    if total and (ok < 3 or ok < 0.9 * total):
        print(f'Key candidate for {home} verifies only {ok} of {total} authenticated frames: not trustworthy.')
        return 1
    print(f'Network key for {home} found' + (f' and verified on {ok} of {total} authenticated frames.' if total
          else '; no authenticated frames to verify it yet (capture a few minutes of normal traffic).'))

    if a.print:
        print(key.hex())
    elif a.check:
        stored = keychain_key()
        print('Keychain: ' + ('no key stored' if stored is None else 'same key' if stored == key else 'DIFFERENT key'))
        return 0 if stored == key else 1
    elif a.store:
        stored = keychain_key()
        if stored == key:
            print('Keychain already holds this key.')
        elif stored is not None and not a.replace:
            sys.exit('The Keychain holds a different key; add --replace to overwrite it.')
        elif store(home, key):
            print(f'Saved in the Keychain (service {SERVICE}, account {home}).')
        else:
            sys.exit('Saving to the Keychain failed.')
    else:
        print('Run again with --store to save it in the Keychain, or --print to show it.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
