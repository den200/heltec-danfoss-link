#!/usr/bin/env python3
"""Summarize the latest diagnostic boot; measurements are not RF acceptance."""
import argparse
import json
from pathlib import Path


def summarize(path):
    events = []
    for line in path.open():
        try:
            row = json.loads(line)
            value = json.loads(row['line'])
        except (ValueError, KeyError, TypeError):
            continue  # UART boot/console text is not a malformed radio packet.
        if value.get('t') == 'boot':
            events = []
        events.append((row['ts'], value))
    statuses = [(ts, v) for ts, v in events if v.get('t') == 'status']
    writes = [{'time': ts, **v} for ts, v in events
              if v.get('t') == 'saved' and not v.get('unchanged')]
    tx = [v for _, v in events if v.get('t') == 'tx']
    latest = statuses[-1] if statuses else None
    return {
        'boot': next(({'time': ts, **v} for ts, v in events if v.get('t') == 'boot'), None),
        'latest_status_time': latest[0] if latest else None,
        'storage': latest[1].get('storage') if latest else None,
        'event_queue_drops': latest[1].get('event_queue_drops') if latest else None,
        'rx_irq_to_task_max_us': latest[1].get('rx_irq_to_task_max_us') if latest else None,
        'max_logged_queue_delay_ms': max((v.get('queue_delay_ms', 0) for _, v in events), default=0),
        'max_reply_processing_us': max((v.get('processing_us', 0) for v in tx), default=None),
        'reply_count': len(tx),
        'checkpoint_writes': writes,
        'storage_errors': [{'time': ts, **v} for ts, v in events
                           if v.get('t') == 'err' and v.get('where') == 'storage'],
        'limitations': 'Latest boot only. ISR-to-task excludes hardware interrupt latency. '
                        'Queued-event time follows radio handling, not packet arrival. '
                        'No proof of missed-packet absence, physical write interruption, '
                        'or simultaneous RF/write acceptance.',
    }


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('journal', type=Path)
    args = parser.parse_args()
    print(json.dumps(summarize(args.journal), indent=2))
