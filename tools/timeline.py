#!/usr/bin/env python3
"""Merged, de-duplicated timeline of every known-network frame in DB exports.

Input: JSONL rows {rowid, ts, line} exported from the listening-post raw table
(te_fsk/fsk zw* windows and the responder firmware's danfoss_report events).
Each frame is printed once per physical transmission (same bytes within 0.5 s
collapse), with MAC status for proprietary payloads.
Usage: timeline.py KEYHEX export.jsonl [...] [--node N]
"""
import datetime
import json
import sys
from zoneinfo import ZoneInfo

from audit_capture import scan
from danfoss_auth import tag
from zwdecode import manchester

TZ = ZoneInfo('Europe/Copenhagen')


def frames_of(m):
    if m.get('t') in ('danfoss_report', 'danfoss_response', 'danfoss_tx', 'dfc_rx', 'dfc_tx') and 'hex' in m:
        f = bytes.fromhex(m['hex'])
        if len(f) >= 10 and f[7] <= len(f):
            f = f[:f[7]]
            c = 0xff
            for b in f[:-1]:
                c ^= b
            if c == f[-1]:
                yield f
        return
    if 'hex' not in m or not str(m.get('tag', '')).startswith('zw'):
        return
    raw = bytes.fromhex(m['hex'])
    for cand in (manchester(raw) if m['tag'].startswith('zw96') else [raw]):
        for f, _ in scan(cand):
            yield f


def mac_status(f, key):
    p = f[9:-1]
    if len(p) < 6 or p[:3] != b'\x91\x00\x02' or p[3] == 0x80:
        return ''
    msg = p[3:-4]
    if len(msg) < 2:
        return ''
    return 'MAC ok' if tag(msg, key) == p[-4:] else 'MAC BAD'


def describe(f):
    src, fc, seq, dst = f[4], f[5], f[6] & 15, f[8]
    kind = {1: 'data', 3: 'ack', 5: 'h5'}.get(fc & 15, f'h{fc & 15}')
    p = f[9:-1]
    s = f'{src:3d}->{dst:3d} {kind:4s} rs={seq:2d}'
    if p[:3] == b'\x91\x00\x02' and len(p) > 5:
        op, ln, body = p[3], p[4], p[5:]
        if op != 0x80:
            body, t = body[:-4], body[-4:]
            s += f' op={op:02x} L={ln:02x} a={body[0]:02x} {body[1:].hex()} | {t.hex()}'
        else:
            s += f' op=80 sub={ln:02x} {body.hex()}'
    elif p:
        s += f' cc={p[:1].hex()} {p[1:].hex()}'
    return s


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    node = None
    if '--node' in sys.argv:
        node = int(sys.argv[sys.argv.index('--node') + 1])
        args.remove(str(node))
    key = bytes.fromhex(args[0])
    seen = {}
    out = []
    for path in args[1:]:
        for line in open(path):
            r = json.loads(line)
            m = json.loads(r['line']) if 'line' in r else r.get('capture', r)
            for f in frames_of(m):
                last = seen.get(f)
                seen[f] = r['ts']
                if last is not None and r['ts'] - last < 0.5:
                    continue
                out.append((r['ts'], f))
    out.sort()
    for ts, f in out:
        if node is not None and node not in (f[4], f[8]):
            continue
        stamp = datetime.datetime.fromtimestamp(ts, TZ).strftime('%m-%d %H:%M:%S.%f')[:-3]
        print(stamp, describe(f), mac_status(f, key))


if __name__ == '__main__':
    main()
