#!/usr/bin/env python3
"""Extract authenticated temperature candidates; no field is labelled verified."""
import argparse
import collections
import datetime as dt
import json
import statistics
from pathlib import Path
from zoneinfo import ZoneInfo

from analyze_head_status import report_fields
from audit_capture import scan
from danfoss_auth import verify_frame
from zwdecode import manchester


def stamp(value):
    zone = ZoneInfo('Europe/Copenhagen')
    if isinstance(value, (int, float)):
        return dt.datetime.fromtimestamp(value, zone)
    parsed = dt.datetime.fromisoformat(value)
    return parsed.replace(tzinfo=zone) if parsed.tzinfo is None else parsed


def candidates(frame):
    layout = report_fields(frame)
    if layout is None:
        return None
    if layout['layout'] == 'routine':
        return {'layout': 'routine',
                'front_sensor_candidate_c': int.from_bytes(frame[40:42], 'big', signed=True) / 100,
                'valve_sensor_candidate_c': int.from_bytes(frame[42:44], 'big', signed=True) / 100,
                'room_temperature_candidate_c': int.from_bytes(frame[44:46], 'big', signed=True) / 100}
    # These layouts contain only one temperature-like word. They must not be
    # treated as routine fields at the same byte offsets.
    offset = 38 if layout['layout'] == 'button' else 44
    return {'layout': layout['layout'], 'single_temperature_candidate_c':
            int.from_bytes(frame[offset:offset + 2], 'big', signed=True) / 100}


def analyze(paths, network):
    key, home = bytes.fromhex(network['authentication_key']), bytes.fromhex(network['home_id'])
    events, seen, counts = [], set(), collections.Counter()
    for path in paths:
        for row_number, line in enumerate(path.open(), 1):
            try:
                row = json.loads(line)
                value = row.get('capture', row.get('line', row))
                if isinstance(value, str):
                    value = json.loads(value)
                raw = bytes.fromhex(value.get('hex', ''))
                when = stamp(row['ts'])
            except (ValueError, KeyError, TypeError, AttributeError):
                continue
            streams = manchester(raw) if value.get('tag', '').startswith('zw96') else [raw]
            for stream in streams:
                for frame, _ in scan(stream, home):
                    fields = candidates(frame)
                    if fields is None or not verify_frame(frame, key):
                        continue
                    identity = (when, frame)
                    if identity in seen:
                        continue
                    seen.add(identity)
                    events.append((when, frame, fields, str(path), row_number))
    events.sort(key=lambda e: e[0])
    heads, last_frame, residuals = {}, {}, []
    for when, frame, fields, path, row_number in events:
        node = frame[4]
        previous = last_frame.get(node)
        duplicate = previous and previous[1] == frame and (when - previous[0]).total_seconds() < 10
        last_frame[node] = (when, frame)
        head = heads.setdefault(str(node), {'routine_reports': 0, 'other_layout_reports': 0})
        evidence = {'time': when.isoformat(timespec='milliseconds'), **fields,
                    'source': path, 'row': row_number, 'hex': frame.hex()}
        head['last_report'] = evidence
        if fields['layout'] == 'routine':
            head['latest_routine'] = evidence
            if not duplicate:
                head['routine_reports'] += 1
                predicted = (4 * fields['front_sensor_candidate_c'] - fields['valve_sensor_candidate_c']) / 3
                residuals.append(abs(predicted - fields['room_temperature_candidate_c']))
        elif not duplicate:
            head['other_layout_reports'] += 1
        counts['authenticated_occurrences'] += 1
    residuals.sort()
    return {'status': 'PROVISIONAL: calibrated CC/reference and thermal-response correlation still required',
            'offsets_zero_based': {'routine': [40, 42, 44], 'button_single_word': 38, 'alternate_single_word': 44},
            'hypothesis': 'Routine words are front sensor, valve sensor, compensated room estimate. '
                          'Third word often approximates (4*first - second)/3; transient deviations mean this is not a proven exact algorithm.',
            'counts': dict(counts), 'routine_compensation_check': {
                'reports': len(residuals), 'median_absolute_residual_c': statistics.median(residuals) if residuals else None,
                'p95_absolute_residual_c': residuals[int((len(residuals) - 1) * .95)] if residuals else None,
                'max_absolute_residual_c': max(residuals) if residuals else None}, 'heads': heads}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('captures', nargs='+', type=Path)
    parser.add_argument('--key-file', type=Path, default=Path(__file__).resolve().parents[1] / 'data/network-key.json')
    args = parser.parse_args()
    print(json.dumps(analyze(args.captures, json.loads(args.key_file.read_text())), indent=2))
