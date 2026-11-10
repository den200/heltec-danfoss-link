#!/usr/bin/env python3
"""Check whether the 16-byte value seen during head inclusion is the key behind the
4-byte tag on Danfoss Link proprietary frames. Tries AES-CMAC and AES-CBC-MAC over many
byte ranges and offsets, matching the first or last 4 bytes of the MAC to the frame tag.

Input: full G.9959 frames (hex), one per line, last byte = G.9959 checksum, the 4 bytes
before it = the tag under test.
"""
import sys
from cryptography.hazmat.primitives.cmac import CMAC
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes

from danfoss_secret import network_key
# Optional argv[1] tests a candidate value; the default is the stored network key.
KEY = bytes.fromhex(sys.argv[1]) if len(sys.argv) > 1 else network_key()
frames = [bytes.fromhex(l.strip()) for l in open(sys.argv[2])] if len(sys.argv) > 2 else []


def cmac(key, data):
    c = CMAC(algorithms.AES(key))
    c.update(data)
    return c.finalize()


def cbcmac(key, data, iv=bytes(16)):
    if len(data) % 16:
        data = data + bytes(16 - len(data) % 16)  # zero pad
    e = Cipher(algorithms.AES(key), modes.CBC(iv)).encryptor()
    return (e.update(data) + e.finalize())[-16:]


def ecb(key, block):
    e = Cipher(algorithms.AES(key), modes.ECB()).encryptor()
    return e.update(block) + e.finalize()


def main():
    tags = [(f, f[-5:-1]) for f in frames]  # 4 bytes before the trailing checksum
    # S0-style derived keys: encrypt a constant block under the network key
    derived = {
        "raw": KEY,
        "auth55": ecb(KEY, b"\x55" * 16),
        "encAA": ecb(KEY, b"\xaa" * 16),
        "ecb00": ecb(KEY, bytes(16)),
    }
    # candidate message = frame bytes [start : end], end measured from the tag position
    for kname, k in derived.items():
        for mac_name, mac in (("cmac", cmac), ("cbcmac", cbcmac)):
            for start in range(0, 15):
                for endtrim in range(0, 8):  # bytes dropped before the tag (incl. the tag itself)
                    for which in ("first4", "last4"):
                        ok = True
                        for f, tag in tags:
                            msg = f[start:len(f) - 1 - endtrim]  # drop checksum + endtrim
                            m = mac(k, msg)
                            cand = m[:4] if which == "first4" else m[-4:]
                            if cand != tag:
                                ok = False
                                break
                        if ok and tags:
                            print(f"MATCH key={kname} {mac_name} start={start} endtrim={endtrim} {which}")
    print(f"tested {len(tags)} frames against key {KEY.hex()}")


if __name__ == "__main__":
    main()
