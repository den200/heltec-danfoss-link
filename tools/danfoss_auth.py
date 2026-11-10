#!/usr/bin/env python3
"""Danfoss Link proprietary four-byte authentication tag, verified on captures.

XTEA (32 cycles), big-endian words, CBC with zero IV. MAC input starts at the
proprietary operation byte (after 91 0002), includes the following length byte,
and ends before the four-byte tag. If not block aligned, append 80 then zeros
up to 8 bytes. Aligned messages receive NO extra block. Tag = final block[4:8].
Pure offline computation; this module does not transmit.
"""
import struct
import hmac

MASK = 0xffffffff

def xtea_encrypt(block, key):
    if len(block) != 8 or len(key) != 16:
        raise ValueError('XTEA requires an 8-byte block and 16-byte key')
    left, right = struct.unpack('>2I', block)
    words = struct.unpack('>4I', key)
    total = 0
    for _ in range(32):
        left = (left + ((((right << 4) ^ (right >> 5)) + right) ^ (total + words[total & 3]))) & MASK
        total = (total + 0x9e3779b9) & MASK
        right = (right + ((((left << 4) ^ (left >> 5)) + left) ^ (total + words[(total >> 11) & 3]))) & MASK
    return struct.pack('>2I', left, right)


def tag(message, key):
    """MAC bytes from operation through body, excluding existing tag/checksum."""
    if len(key) != 16 or len(message) < 2:
        raise ValueError('Expected 16-byte key and operation/length/message bytes')
    if len(message) % 8:
        message += b'\x80'
        message += bytes(-len(message) % 8)
    state = bytes(8)
    for offset in range(0, len(message), 8):
        state = xtea_encrypt(bytes(a ^ b for a, b in zip(state, message[offset:offset+8])), key)
    return state[4:]


def verify_frame(frame, key):
    """Verify captured direct singlecast frame; reject unsupported framing."""
    if len(frame) < 20 or frame[7] != len(frame):
        return False
    if frame[5] & 0x8f != 1 or frame[9:12] != bytes.fromhex('910002'):
        return False
    if frame[12] not in (0x10, 0x50, 0x90):
        return False
    checksum = 0xff
    for byte in frame[:-1]:
        checksum ^= byte
    return checksum == frame[-1] and hmac.compare_digest(tag(frame[12:-5], key), frame[-5:-1])
