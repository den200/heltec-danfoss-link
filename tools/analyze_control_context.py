#!/usr/bin/env python3
"""Offline, authenticated correlation of button actions and CC reply context.

Only serial 'cc' observations count as controller traffic; bridge TX never does.
No interpretation of the raw context word as a counter/mode is assumed.
"""
import argparse
import collections
import datetime as dt
import json
from pathlib import Path
from zoneinfo import ZoneInfo

from analyze_head_status import report_fields
from danfoss_auth import verify_frame


def timestamp(value):
    parsed = dt.datetime.fromisoformat(value)
    return parsed.replace(tzinfo=ZoneInfo('Europe/Copenhagen')) if parsed.tzinfo is None else parsed


def analyze(path, network):
    key = bytes.fromhex(network['authentication_key'])
    home = bytes.fromhex(network['home_id'])
    events, seen, counts = [], set(), collections.Counter()
    for number, line in enumerate(path.open(), 1):
        try:
            row = json.loads(line)
            message = json.loads(row['line'])
            when = timestamp(row['ts'])
            frame = bytes.fromhex(message.get('hex', ''))
        except (KeyError, ValueError, TypeError, AttributeError):
            continue
        if not frame or frame[:4] != home or not verify_frame(frame, key):
            continue
        identity = (when, frame, message.get('t'))
        if identity in seen:
            continue
        seen.add(identity)
        events.append((when, message, frame, number))
    events.sort(key=lambda event: event[0])
    previous, pending, last_button = {}, {}, {}
    button_pairs, transitions, unmatched = [], [], []
    for when, message, frame, number in events:
        kind = message.get('t')
        reference = {'time': when.isoformat(timespec='milliseconds'), 'row': number, 'hex': frame.hex()}
        fields = report_fields(frame)
        if kind in ('report', 'dup') and fields and fields['layout'] == 'button':
            node = frame[4]
            old = last_button.get(node)
            if old and old[1] == frame[12:-5] and (when - old[0]).total_seconds() < 10:
                continue
            last_button[node] = (when, frame[12:-5])
            if node in pending:
                unmatched.append(pending.pop(node))
            pending[node] = {'node': node, 'local_target_c100': fields['local_target_c100'],
                             'button': reference, 'before': previous.get(node)}
        if kind != 'cc' or frame[4] != 1 or len(frame) not in (27, 37):
            continue
        if frame[12] not in (0x10, 0x50) or frame[14] & 0xf8 != 8 or frame[15] != 0x0d:
            continue
        node = frame[8]
        current = {**reference, 'node': node, 'operation': frame[12], 'length': len(frame),
                   'context_raw': frame[16:20].hex(), 'context_u32_be_hypothesis': int.from_bytes(frame[16:20], 'big'),
                   'target_c100': int.from_bytes(frame[20:22], 'big'),
                   'secondary_c100': int.from_bytes(frame[22:24], 'big') if len(frame) == 37 else None,
                   'tail_24_31': frame[24:32].hex() if len(frame) == 37 else None}
        old = previous.get(node)
        if old and (old['context_raw'], old['target_c100']) != (current['context_raw'], current['target_c100']):
            transitions.append({'node': node, 'before': old, 'after': current,
                                'target_changed': old['target_c100'] != current['target_c100'],
                                'context_changed': old['context_raw'] != current['context_raw']})
        if node in pending:
            pair = pending.pop(node)
            latency = (when - timestamp(pair['button']['time'])).total_seconds()
            if 0 <= latency <= 2:
                pair.update(reply=current, latency_s=round(latency, 3),
                            echoes_local_target=current['target_c100'] == pair['local_target_c100'])
                if pair['before']:
                    pair['context_delta_u32_hypothesis'] = (current['context_u32_be_hypothesis'] -
                        pair['before']['context_u32_be_hypothesis']) & 0xffffffff
                button_pairs.append(pair)
            else:
                unmatched.append(pair)
        previous[node] = current
        counts['authenticated_cc_target_replies'] += 1
    unmatched.extend(pending.values())
    counts['button_reply_pairs'] = len(button_pairs)
    counts['paired_target_echoes'] = sum(pair['echoes_local_target'] for pair in button_pairs)
    counts['target_changes_without_context_change'] = sum(t['target_changed'] and not t['context_changed'] for t in transitions)
    counts['context_changes_without_target_change'] = sum(t['context_changed'] and not t['target_changed'] for t in transitions)
    return {'status': 'Observed CC behavior only; link delivery and CC transmission do not prove head application',
            'source': str(path), 'counts': dict(counts), 'button_reply_pairs': button_pairs,
            'transitions': transitions, 'unmatched_buttons': unmatched}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('journal', type=Path)
    parser.add_argument('--key-file', type=Path, default=Path(__file__).resolve().parents[1] / 'data/network-key.json')
    args = parser.parse_args()
    print(json.dumps(analyze(args.journal, json.loads(args.key_file.read_text())), indent=2))
