#!/usr/bin/env python3
"""Search CRC-16 parameters that explain the 2-byte trailer of Danfoss proprietary frames.
Args: hex payloads starting at the 0x91 command class byte, ending with the 2 trailer bytes."""
import sys

def table(poly):
    t = []
    for i in range(256):
        c = i << 8
        for _ in range(8):
            c = ((c << 1) ^ poly) & 0xFFFF if c & 0x8000 else (c << 1) & 0xFFFF
        t.append(c)
    return t

REV8 = [int(f"{i:08b}"[::-1], 2) for i in range(256)]
def rev16(x): return (REV8[x & 0xFF] << 8) | REV8[x >> 8]

frames = [bytes.fromhex(h) for h in sys.argv[1:]]
polys = range(1, 0x10000) if "-all" in sys.argv[0:1] else range(1, 0x10000)
for poly in polys:
    t = table(poly)
    for refin in (0, 1):
        for start in range(0, 6):
            # with init/xorout unknown, compute with init 0 and solve: crc(init) = crc0 ^ f(init); try common inits
            for init in (0, 0xFFFF, 0x1D0F, 0xC6C6):
                res = []
                for fr in frames:
                    c = init
                    for b in fr[start:-2]:
                        if refin: b = REV8[b]
                        c = ((c << 8) & 0xFFFF) ^ t[(c >> 8) ^ b]
                    if refin: c = rev16(c)
                    res.append(c)
                for endian in ("big", "little"):
                    x = [r ^ int.from_bytes(fr[-2:], endian) for r, fr in zip(res, frames)]
                    if len(set(x)) == 1:
                        print(f"poly={poly:#06x} init={init:#06x} ref={refin} xorout={x[0]:#06x} start={start} {endian}")
