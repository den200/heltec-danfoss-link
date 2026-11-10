#!/usr/bin/env python3
"""Offline audit of exported RF windows; scan every alignment for a known Home ID.

Retains each physical occurrence, including multiple frames in one receiver window.
An XOR check is error detection, not authentication. Does not decode other Home IDs.
Usage: python3 tools/audit_capture.py data/raw-20261020-2320-2350.jsonl
"""
import collections
import datetime
import json
import sys
from zoneinfo import ZoneInfo
from zwdecode import checksum, describe, manchester, shift


def scan(raw, home=bytes.fromhex('ffd2bb2d')):
    found = {}
    for inv in (False, True):
        data = bytes(b ^ 255 for b in raw) if inv else raw
        for bits in range(8):
            data_shifted = shift(data, bits)
            pos = 0
            while True:
                pos = data_shifted.find(home, pos)
                if pos < 0:
                    break
                remaining = data_shifted[pos:]
                if len(remaining) >= 10:
                    n = remaining[7]
                    # Do not accept the zero-filled bits introduced by shift().
                    if 10 <= n and (pos+n)*8+bits <= len(raw)*8:
                        f = remaining[:n]
                        if checksum(f[:-1]) == f[-1]:
                            found[(pos*8+bits, f)] = (f, f"{'inv' if inv else 'norm'}+{bits}@{pos}")
                pos += 1
    return [found[k] for k in sorted(found)]


def main():
    counts = collections.Counter()
    for row, line in enumerate(open(sys.argv[1]), 1):
        obj = json.loads(line)
        m = obj['capture']
        if 'hex' not in m:
            counts['metadata'] += 1
            continue
        counts[m['tag']] += 1
        raw = bytes.fromhex(m['hex'])
        candidates = manchester(raw) if m['tag'].startswith('zw96') else [raw]
        for phase, candidate in enumerate(candidates):
            for frame, how in scan(candidate):
                counts['frames'] += 1
                stamp = datetime.datetime.fromtimestamp(obj['ts'], ZoneInfo('Europe/Copenhagen')).isoformat(timespec='milliseconds')
                print(stamp, f'row={row}', m['tag'], f'phase={phase}', how, describe(frame), frame.hex())
    print(dict(counts), file=sys.stderr)


if __name__ == '__main__':
    main()
