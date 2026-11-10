#!/usr/bin/env python3
"""Bound a spare16 trial through the capture command spool; never open serial.

Start as an independent host service before enabling control. Session JSON must
contain an aware ISO deadline, node16 and cc_confirmed_off=true. An early end
does not cancel the guard unless disabled control was explicitly verified.
"""
import argparse
import datetime as dt
import json
import time
from pathlib import Path


def atomic_json(path, value):
    temporary = path.with_suffix('.tmp')
    temporary.write_text(json.dumps(value, indent=2) + '\n')
    temporary.replace(path)


def stop(root, session_path, timeout=45):
    when = dt.datetime.now().astimezone()
    # Unique names preserve earlier guard evidence and sort SHADOW before STATUS.
    label = 'guard-' + str(time.time_ns())
    for suffix, command in [('0-shadow', 'SHADOW'), ('1-status', 'STATUS')]:
        path = root / 'commands' / (label + '-' + suffix + '.tmp')
        path.write_text(command + '\n')
        path.rename(path.with_suffix('.cmd'))
    result = {'shadow_queued': when.isoformat(), 'verified_disabled': False}
    until = time.monotonic() + timeout
    while time.monotonic() < until:
        for line in (root / 'raw.jsonl').read_text().splitlines()[-100:]:
            try:
                row = json.loads(line)
                event = json.loads(row['line'])
                timestamp = dt.datetime.fromisoformat(row['ts'])
                if timestamp.tzinfo is None or timestamp < when:
                    continue
                if event.get('t') != 'status' or not event.get('heads'):
                    continue
                if (all(not h['enabled'] and not h['pending'] for h in event['heads'])
                        and not event['storage']['autostart'] and not event['storage']['dirty']):
                    result.update(verified_disabled=True, status_time=row['ts'])
            except (KeyError, TypeError, ValueError):
                continue
        if result['verified_disabled']:
            break
        time.sleep(0.2)
    session = json.loads(session_path.read_text())
    session['stop_requested_at'] = result['shadow_queued']
    session['control_disabled_verified'] = result['verified_disabled']
    if result['verified_disabled']:
        # Conservatively use verified status time, not the command queue time.
        session['end'] = result['status_time']
        session['end_reason'] = 'bounded host guard; saved SHADOW verified'
    atomic_json(session_path, session)
    atomic_json(session_path.with_name(session_path.stem + '-guard-result.json'), result)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('root', type=Path)
    parser.add_argument('session', type=Path)
    args = parser.parse_args()
    session = json.loads(args.session.read_text())
    deadline = dt.datetime.fromisoformat(session['deadline'])
    if session['node'] != 16 or not session.get('cc_confirmed_off') or deadline.tzinfo is None:
        raise ValueError('Requires spare16, explicit CC-off confirmation and aware deadline')
    if (deadline - dt.datetime.now().astimezone()).total_seconds() > 600:
        raise ValueError('Bench deadline must be at most ten minutes away')
    while dt.datetime.now().astimezone() < deadline:
        if json.loads(args.session.read_text()).get('control_disabled_verified'):
            return
        time.sleep(0.2)
    result = stop(args.root, args.session)
    print(json.dumps(result), flush=True)
    if not result['verified_disabled']:
        raise SystemExit('SHADOW queued but saved disabled state not verified')


if __name__ == '__main__':
    main()
