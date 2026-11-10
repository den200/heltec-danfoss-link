#!/usr/bin/env python3
"""Self-test for extract_key.py with a made-up key: no capture or real key needed."""
import json, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import extract_key as ek
from danfoss_auth import tag

KEY = bytes(range(16))
HOME = bytes.fromhex('c0ffee01')


def frame(src, dst, payload, fc1=0x41, seq=1):
    f = HOME + bytes([src, fc1, seq, 9 + len(payload) + 1, dst]) + payload
    c = 0xff
    for x in f:
        c ^= x
    return f + bytes([c])


def tagged(src, dst, body, key=KEY):
    mac_input = bytes([0x10, len(body) + 2]) + body
    return frame(src, dst, b'\x91\x00\x02' + mac_input + tag(mac_input, key))


def shifted_window(f, bits):
    """As a raw receiver would see it: inverted, preamble, SOF, f, at a bit offset."""
    wire = bytes([0xaa] * 4 + [0x0f]) + bytes(x ^ 0xff for x in f) + b'\x00\x00'
    v = int.from_bytes(wire, 'big') >> bits
    return v.to_bytes(len(wire), 'big').hex()


pairing = frame(1, 7, b'\x91\x00\x02\x80\x04' + KEY)
reports = [tagged(7, 1, bytes([0x0c, i, 0x20, 0x30, 0, 0, 0, 0])) for i in range(5)]
lines = [json.dumps({'t': 'frame', 'hex': pairing.hex()})]                    # capture firmware
lines += [json.dumps({'t': 'frame', 'hex': f.hex()}) for f in reports[:3]]
lines += [json.dumps({'t': 'fsk', 'hex': shifted_window(f, 3)}) for f in reports[3:]]  # listening post

homes = ek.analyse(lines)
h = homes[HOME.hex()]
assert h['keys'] == {KEY}, h['keys']
assert len(h['tagged']) == 5 and all(ek.verifies(f, KEY) for f in h['tagged'])
assert not any(ek.verifies(f, bytes(16)) for f in h['tagged'])
assert h['nodes'] == {1, 7}
print('extract_key self-test passed')
