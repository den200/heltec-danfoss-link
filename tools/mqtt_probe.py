#!/usr/bin/env python3
"""Read bridge MQTT evidence using private config, without logging credentials."""
import argparse
import datetime as dt
import json
import socket
import struct
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def field(value):
    value = value.encode()
    return struct.pack('!H', len(value)) + value


def send(sock, kind, body):
    size, header = len(body), bytearray([kind])
    while True:
        digit = size % 128
        size //= 128
        header.append(digit | (128 if size else 0))
        if not size:
            break
    sock.sendall(header + body)


def exact(sock, count):
    out = bytearray()
    while len(out) < count:
        chunk = sock.recv(count - len(out))
        if not chunk:
            raise ConnectionError('MQTT broker closed the connection')
        out.extend(chunk)
    return bytes(out)


def packet(sock):
    kind = exact(sock, 1)[0]
    size = 0
    for shift in range(0, 28, 7):
        digit = exact(sock, 1)[0]
        size |= (digit & 127) << shift
        if size > 65536:
            raise ValueError('MQTT packet exceeds probe limit')
        if not digit & 128:
            return kind, exact(sock, size)
    raise ValueError('Invalid MQTT remaining length')


def connect(client_id=None):
    config = json.loads((ROOT / 'data/heltec-network.private.json').read_text())
    sock = socket.create_connection((config['mqtt_host'], config['mqtt_port']), timeout=4)
    body = b'\x00\x04MQTT\x04\xc2\x00\x1e' + field(client_id or 'danfoss_probe_' + str(time.time_ns()))
    body += field(config['mqtt_username']) + field(config['mqtt_password'])
    send(sock, 0x10, body)
    kind, response = packet(sock)
    if kind != 0x20 or response != b'\x00\x00':
        sock.close()
        raise ConnectionError('MQTT login rejected; broker return code ' + str(response[-1] if response else -1))
    return sock


def listen(sock, seconds):
    send(sock, 0x82, b'\x00\x01' + field('danfoss/ffd2bb2d/#') + b'\x00' +
         field('homeassistant/+/danfoss_ffd2bb2d/+/config') + b'\x00')
    end, events = time.monotonic() + seconds, []
    last_ping = time.monotonic()
    while time.monotonic() < end:
        now = time.monotonic()
        if now - last_ping >= 15:
            send(sock, 0xc0, b'')
            last_ping = now
        sock.settimeout(max(0.1, min(end - now, 15 - (now - last_ping))))
        try:
            kind, body = packet(sock)
        except socket.timeout:
            continue
        if kind >> 4 != 3:
            continue
        topic_size = struct.unpack('!H', body[:2])[0]
        offset = 2 + topic_size
        if (kind >> 1) & 3:
            offset += 2
        payload = body[offset:].decode(errors='replace')
        try:
            payload = json.loads(payload)
        except ValueError:
            pass
        events.append({'time': dt.datetime.now().astimezone().isoformat(),
                       'topic': body[2:2 + topic_size].decode(), 'retained': bool(kind & 1),
                       'payload': payload})
    return events


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--seconds', type=int, default=8)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    with connect() as sock:
        events = listen(sock, args.seconds)
        send(sock, 0xe0, b'')
    result = {'authenticated': True, 'events': events}
    if args.output:
        args.output.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))
