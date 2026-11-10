#!/usr/bin/env python3
"""Decode Z-Wave (ITU-T G.9959, R1/R2 framing) frames captured by the listening-post T-Embed.

Reads te_fsk lines with a zw* tag from the listening-post SQLite db, finds frames whose
XOR checksum is valid (trying both bit polarities and small bit slips), and prints them.

Usage: zwdecode.py [db] [minutes back]
"""
import json
import os
import sqlite3
import sys
import time

CC = {
    0x00: "NO_OPERATION", 0x20: "BASIC", 0x25: "SWITCH_BINARY", 0x26: "SWITCH_MULTILEVEL",
    0x31: "SENSOR_MULTILEVEL", 0x40: "THERMOSTAT_MODE", 0x42: "THERMOSTAT_OPERATING_STATE",
    0x43: "THERMOSTAT_SETPOINT", 0x46: "CLIMATE_CONTROL_SCHEDULE", 0x47: "THERMOSTAT_SETBACK",
    0x56: "CRC16_ENCAP", 0x60: "MULTI_CHANNEL", 0x70: "CONFIGURATION", 0x72: "MANUFACTURER_SPECIFIC",
    0x75: "PROTECTION", 0x80: "BATTERY", 0x81: "CLOCK", 0x84: "WAKE_UP", 0x85: "ASSOCIATION",
    0x86: "VERSION", 0x8F: "MULTI_CMD", 0x98: "SECURITY_S0", 0x9F: "SECURITY_S2",
    0xF0: "NON_INTEROP", 0x01: "ZWAVE_PROTOCOL",
}
HDR = {1: "singlecast", 2: "multicast", 3: "ack", 8: "routed"}


def checksum(b):
    c = 0xFF
    for x in b:
        c ^= x
    return c


def shift(data, bits):
    """Shift a byte string left by 0-7 bits (drop the first bits)."""
    if not bits:
        return data
    v = int.from_bytes(data, "big") << bits
    return (v & ((1 << (8 * len(data))) - 1)).to_bytes(len(data), "big")


def frames(raw):
    """Yield (frame bytes, how) for all checksum-valid G.9959 frames in raw.

    The capture may start at the SOF-synced frame (sync 55F0) or anywhere in the preamble
    (sync 5555), so also look for a 0xF0 start-of-frame byte at every bit offset.
    """
    seen = set()
    for inv in (False, True):
        d = bytes(x ^ 0xFF for x in raw) if inv else raw
        for bits in range(8):
            s = shift(d, bits)
            starts = [0] + [i + 1 for i in range(len(s) - 1) if s[i] == 0xF0 and (i == 0 or s[i - 1] in (0x55, 0xAA))]
            # Some windows have clipped/noisy preambles. Recover our known network
            # there too; sync-based starts above remain unrestricted by Home ID.
            starts += [i for i in range(len(s) - 3) if s[i:i+4] == bytes.fromhex("ffd2bb2d")]
            for st in sorted(set(starts)):
                f = s[st:]
                if len(f) < 10:
                    continue
                n = f[7]
                if 10 <= n <= len(f) and (st + n) * 8 + bits <= len(raw) * 8 and checksum(f[: n - 1]) == f[n - 1]:
                    identity = (st * 8 + bits, f[:n])
                    if identity not in seen:
                        seen.add(identity)
                        yield f[:n], f"{'inv' if inv else 'norm'}+{bits}@{st}"


def manchester(raw):
    """R1 (9.6 kbps) is Manchester coded and captured as NRZ chips at 19.2 kBd.
    Return the decoded byte strings for both chip phases (polarity is left to frames())."""
    bits = "".join(f"{b:08b}" for b in raw)
    out = []
    for phase in (0, 1):
        dec = []
        for i in range(phase, len(bits) - 1, 2):
            pair = bits[i:i + 2]
            if pair == "10":
                dec.append("1")
            elif pair == "01":
                dec.append("0")
            else:
                if len(dec) >= 80:
                    run = "".join(dec)
                    out.append(int(run[:len(run)//8*8], 2).to_bytes(len(run)//8, "big"))
                dec = []  # preserve completed runs, then continue after noise
        dec = "".join(dec)
        if len(dec) >= 80:
            out.append(int(dec[: len(dec) // 8 * 8], 2).to_bytes(len(dec) // 8, "big"))
    return out


def describe(f):
    home, src, fc1, fc2, ln, dst = f[:4].hex(), f[4], f[5], f[6], f[7], f[8]
    payload = f[9:-1]
    htype = HDR.get(fc1 & 0x0F, f"h{fc1 & 0x0F}")
    flags = []
    if fc1 & 0x80: flags.append("routed")
    if fc1 & 0x40: flags.append("ackreq")
    if fc1 & 0x20: flags.append("lowpwr")
    if fc1 & 0x10: flags.append("speedmod")
    s = f"home={home} {src:3d}->{dst:3d} {htype}{'/' + ','.join(flags) if flags else ''} seq={fc2 & 0x0F}"
    if payload and (fc1 & 0x0F) != 3:
        if fc1 & 0x80 and len(payload) >= 2:
            s += " [routed hdr " + payload[:2 + (payload[1] >> 4)].hex() + "]"
            payload = payload[2 + (payload[1] >> 4):]
        if payload and payload[0] == 0x91 and len(payload) >= 3:
            # Manufacturer Proprietary: 2-byte manufacturer ID (0x0002 = Danfoss), then vendor data
            # op=80 uses values 02/04/05 that do not match the remaining byte count.
            field = "field" if payload[3:4] == b"\x80" else "len"
            s += f" PROPRIETARY mfr={payload[1:3].hex()} op={payload[3:4].hex()} {field}={payload[4:5].hex()} data={payload[5:].hex()}"
        elif payload:
            s += f" CC={CC.get(payload[0], hex(payload[0]))} cmd={payload[1:2].hex()} data={payload[2:].hex()}"
    return s


def main():
    db = sys.argv[1] if len(sys.argv) > 1 else os.path.expanduser("~/listening-post/lp.db")
    mins = float(sys.argv[2]) if len(sys.argv) > 2 else 60
    con = sqlite3.connect(f"file:{db}?mode=ro", uri=True)
    # te_fsk: T-Embed CC1101, fsk: Heltec SX1262
    rows = con.execute("""SELECT ts, line FROM raw WHERE ts > ? AND (line LIKE '{"t":"te_fsk"%' OR line LIKE '{"t":"fsk"%')
                          AND line LIKE '%"tag":"zw%' ORDER BY ts""",
                       (time.time() - mins * 60,))
    n = good = 0
    for ts, line in rows:
        m = json.loads(line)
        n += 1
        if "hex" not in m:
            continue
        raw = bytes.fromhex(m["hex"])
        hit = False
        # zw96* windows hold Manchester chips (R1), the others NRZ bits (R2)
        cands = manchester(raw) if m["tag"].startswith("zw96") else [raw]
        for f, how in (x for c in cands for x in frames(c)):
            good += 1
            hit = True
            print(time.strftime("%H:%M:%S", time.localtime(ts)), f"{m['rssi']:6.1f}dBm", m["tag"], how, describe(f), f.hex())
        if not hit and "-v" in sys.argv:
            print(time.strftime("%H:%M:%S", time.localtime(ts)), f"{m['rssi']:6.1f}dBm", m["tag"], "nochk", raw.hex())
    print(f"{n} captures, {good} valid frames", file=sys.stderr)


if __name__ == "__main__":
    main()
