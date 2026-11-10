#!/usr/bin/env python3
"""Compare authenticated head status words without guessing battery semantics.

Accepts SQLite exports (line/capture) and bridge serial journals; receive-only.
Example: python3 tools/analyze_head_status.py data/battery-capture-20261021.jsonl \
    data/bridge-shadow-20261021.jsonl
"""
import argparse
import collections
import datetime
import json
from pathlib import Path
from zoneinfo import ZoneInfo

from audit_capture import scan
from danfoss_auth import verify_frame
from zwdecode import manchester

SCHEMAS = {
    (57, bytes.fromhex('4720202021030020102011030f031703280318032f0330')): 'routine',
    (57, bytes.fromhex('85030020102011030f031703280318032f03300301030203030323')): 'alternate',
    (53, bytes.fromhex('46030020102011030f0317030103280318032f0330')): 'button',
}


def report_fields(frame):
    """Return observed layout and raw fields; caller must authenticate first."""
    if len(frame) not in (53, 57) or not 1 < frame[4] < 233 or frame[8] != 1:
        return None
    if frame[12] != 0x10 or frame[14] & 0xf8 != 0x10 or frame[15] != 0x0c:
        return None
    for (length, prefix), kind in SCHEMAS.items():
        if len(frame) == length and frame[17:17 + len(prefix)] == prefix:
            off = len(frame) - (9 if kind == 'button' else 7)
            result = {'layout': kind, 'status_raw': frame[off:off + 2].hex()}
            if kind == 'button':
                result['local_target_c100'] = int.from_bytes(frame[-7:-5], 'big')
            return result
    return None


def local_time(value):
    if isinstance(value, (int, float)):
        return datetime.datetime.fromtimestamp(value, ZoneInfo('Europe/Copenhagen')).isoformat(timespec='milliseconds')
    return value  # bridge journal timestamps were recorded in Pi local time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('captures', nargs='+', type=Path)
    parser.add_argument('--key-file', type=Path, default=Path(__file__).resolve().parents[1] / 'data/network-key.json')
    args = parser.parse_args()
    network = json.loads(args.key_file.read_text())
    key = bytes.fromhex(network['authentication_key'])
    home = bytes.fromhex(network['home_id'])
    stats = collections.Counter()
    heads = collections.defaultdict(lambda: {'occurrences': 0, 'frames': set(), 'layouts': collections.Counter(), 'status_raw': collections.Counter(), 'button_events': []})
    for path in args.captures:
        with path.open() as source:
            for row_number, line in enumerate(source, 1):
                stats['rows'] += 1
                try:
                    row = json.loads(line)
                    message = row.get('capture', row.get('line', row))
                    if isinstance(message, str):
                        message = json.loads(message)
                    raw = bytes.fromhex(message.get('hex', ''))
                except (ValueError, TypeError, AttributeError):
                    stats['malformed_rows'] += 1
                    continue
                if not raw:
                    continue
                candidates = manchester(raw) if message.get('tag', '').startswith('zw96') else [raw]
                for candidate in candidates:
                    for frame, _ in scan(candidate, home):
                        if not verify_frame(frame, key):
                            continue
                        stats['authenticated_occurrences'] += 1
                        fields = report_fields(frame)
                        if fields is None:
                            continue
                        head = heads[frame[4]]
                        head['occurrences'] += 1
                        if frame in head['frames']:
                            continue
                        head['frames'].add(frame)
                        head['layouts'][fields['layout']] += 1
                        head['status_raw'][fields['status_raw']] += 1
                        if fields['layout'] == 'button':
                            head['button_events'].append({
                                'time': local_time(row.get('ts')), 'source': str(path),
                                'row': row_number, **fields, 'hex': frame.hex(),
                            })
    result = {'note': 'Raw status only: node 6 changed fe00 to 4f00 after fresh batteries; see data/BATTERY-STATUS.md for labelled evidence.', 'counts': dict(stats), 'heads': {}}
    for node, head in sorted(heads.items()):
        head['distinct_frames'] = len(head.pop('frames'))
        result['heads'][str(node)] = head
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
