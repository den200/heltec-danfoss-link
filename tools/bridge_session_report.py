#!/usr/bin/env python3
"""Summarize a bounded bridge control trial without declaring visual tests passed.

Usage: bridge_session_report.py JOURNAL --session SESSION_JSON
All packet counts are local receiver observations, not proof of complete RF capture.
"""
import argparse
import collections
import datetime as dt
import json
from pathlib import Path
from zoneinfo import ZoneInfo


def timestamp(value):
    result = dt.datetime.fromisoformat(value)
    return result.replace(tzinfo=ZoneInfo('Europe/Copenhagen')) if result.tzinfo is None else result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('journal', type=Path)
    parser.add_argument('--session', required=True, type=Path)
    args = parser.parse_args()
    session = json.loads(args.session.read_text())
    node, start = session['node'], timestamp(session['start'])
    end = timestamp(session['end']) if session.get('end') else None
    counts = collections.Counter()
    tx, acks, targets, boots, reports, stops = [], [], [], [], [], []
    last_status = None
    last_mode = None
    latest = start
    for line in args.journal.open():
        try:
            row = json.loads(line)
            when = timestamp(row['ts'])
            if when < start or (end is not None and when > end):
                continue
            latest = max(latest, when)
        except (KeyError, ValueError, TypeError):
            counts['malformed_journal_records'] += 1
            continue
        try:
            message = json.loads(row['line'])
            if not isinstance(message, dict):
                raise ValueError('not an event object')
        except (KeyError, ValueError, TypeError):
            counts['non_json_console_records'] += 1
            continue
        kind = message.get('t')
        if kind == 'mode':
            last_mode = {'time': row['ts'], **message}
        if kind == 'boot':
            boots.append({'time': row['ts'], 'firmware': message.get('fw')})
        if kind == 'status':
            for head in message['heads']:
                if head['node'] == node:
                    age_ms = (message['ms'] - head['last_seen']) & 0xffffffff
                    last_status = {'time': row['ts'], 'report_age_seconds': round(age_ms / 1000, 2) if head['reports'] else None, **head}
        if (kind == 'mode' and message.get('shadow')) or (kind == 'control' and
                message.get('node') == node and not message.get('enabled')):
            stops.append(when)
        if (kind == 'set' or (kind == 'mqtt_command' and message.get('result') == 'queued')) and message.get('node') == node:
            targets.append({'time': row['ts'], 'target_c100': message['target'],
                            'transport': 'mqtt' if kind == 'mqtt_command' else 'serial'})
        if message.get('node') == node:
            counts[kind] += 1
            if kind == 'tx':
                tx.append((when, message))
            elif kind == 'ack':
                acks.append((when, message))
            elif kind == 'report':
                reports.append(when)
    matched = 0
    used = set()
    for when, message in tx:
        frame = bytes.fromhex(message['hex'])
        for index, (ack_time, ack) in enumerate(acks):
            if index in used or abs((ack_time - when).total_seconds()) > 1:
                continue
            f = bytes.fromhex(ack['hex'])
            if len(f) == 10 and f[4] == node and f[8] == 1 and (f[6] & 15) == (frame[6] & 15):
                matched += 1
                used.add(index)
                break
    first_tx = tx[0][0] if tx else None
    elapsed = max(0, (latest - first_tx).total_seconds()) if first_tx else 0
    last_tx = tx[-1][0] if tx else None
    interrupted = bool(first_tx and (any(first_tx < timestamp(b['time']) <= last_tx for b in boots) or
                                    any(first_tx < stop < last_tx for stop in stops)))
    reply_span = (last_tx - first_tx).total_seconds() if first_tx and not interrupted else None
    result = {
        'node': node, 'trial_start': session['start'], 'trial_end': session.get('end'), 'latest_record': latest.isoformat(),
        'first_reply': first_tx.isoformat() if first_tx else None,
        'observed_minutes_since_first_reply': round(elapsed / 60, 2),
        'uninterrupted_reply_span_minutes': round(reply_span / 60, 2) if reply_span is not None else None,
        'interruption_between_replies': interrupted,
        'local_event_counts': dict(counts), 'replies_with_matching_ack': matched,
        'transmit_api_failures': sum(m.get('ack_status', 0) != 0 or m.get('response_status', 0) != 0 for _, m in tx),
        'distinct_report_events': len(reports),
        'largest_report_gap_minutes': round(max(((b - a).total_seconds() for a, b in zip(reports, reports[1:])), default=0) / 60, 2),
        'target_commands': targets, 'boot_events': boots, 'latest_head_status': last_status,
        'latest_mode_event': last_mode,
        'one_hour_elapsed': reply_span is not None and reply_span >= 3600,
        'acceptance': 'NOT AUTOMATICALLY PASSED: combine radio evidence with labelled physical checks and trial-specific requirements.',
    }
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
