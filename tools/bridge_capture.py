#!/usr/bin/env python3
"""Single serial owner for bridge capture and commands, with read-only STATUS polls.

Usage: bridge_capture.py JOURNAL --commands COMMAND_DIR [--port PORT]
Queue one command per *.cmd file using an atomic rename. Sent files become *.sent;
*.sending files after a crash are deliberately not replayed (delivery is uncertain).
"""
import argparse
import datetime
import glob
import json
from pathlib import Path
import time


def record(out, line):
    out.write(json.dumps({'ts': datetime.datetime.now().astimezone().isoformat(timespec='milliseconds'), 'line': line}) + '\n')


def send_queued(port, directory, out):
    for path in sorted(directory.glob('*.cmd')):
        command = path.read_text().strip()
        if not command or len(command) > 90 or '\n' in command or '\r' in command or not command.isascii():
            path.rename(path.with_suffix('.rejected'))
            record(out, json.dumps({'t': 'host_rejected', 'id': path.name}))
            continue
        sending = path.with_suffix('.sending')
        path.rename(sending)
        port.write((command + '\n').encode('ascii'))
        port.flush()
        record(out, json.dumps({'t': 'host_command', 'id': path.name, 'command': command}))
        sending.rename(path.with_suffix('.sent'))


def main():
    import serial
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('journal', type=Path)
    parser.add_argument('--commands', required=True, type=Path)
    parser.add_argument('--port')
    args = parser.parse_args()
    args.commands.mkdir(parents=True, exist_ok=True)
    args.journal.parent.mkdir(parents=True, exist_ok=True)
    with args.journal.open('a', buffering=1) as out:
        while True:
            try:
                ports = [args.port] if args.port else sorted(glob.glob('/dev/serial/by-id/*CP210*'))
                if len(ports) != 1:
                    raise OSError('Expected exactly one Heltec serial port')
                connection = serial.Serial(port=None, baudrate=115200, timeout=0.2, write_timeout=2, exclusive=True)
                connection.dtr = False
                connection.rts = False
                connection.port = ports[0]
                connection.open()
                with connection as port:
                    record(out, json.dumps({'t': 'host_connected', 'port': ports[0]}))
                    buf = b''
                    last_status = 0
                    while True:
                        send_queued(port, args.commands, out)
                        if time.monotonic() - last_status >= 30:
                            port.write(b'STATUS\n')
                            port.flush()
                            last_status = time.monotonic()
                        buf += port.read(port.in_waiting or 1)
                        while b'\n' in buf:
                            line, buf = buf.split(b'\n', 1)
                            line = line.decode('utf-8', 'replace').strip()
                            if line:
                                record(out, line)
                        if len(buf) > 16384:
                            record(out, json.dumps({'t': 'host_truncated_line', 'bytes': len(buf)}))
                            buf = b''
            except (serial.SerialException, OSError) as error:
                record(out, json.dumps({'t': 'host_error', 'error': str(error)}))
                time.sleep(2)


if __name__ == '__main__':
    main()
