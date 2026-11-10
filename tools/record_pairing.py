#!/usr/bin/env python3
"""One-hour receive-only session on rpi5, with raw journal and timer restoration."""
import datetime
import json
import os
from pathlib import Path
import signal
import sqlite3
import subprocess
import time

base = Path.home() / 'listening-post'
root = Path.home() / 'danfoss-captures'
root.mkdir(exist_ok=True)
folder = root / datetime.datetime.now().strftime('%Y%m%d-%H%M%S')
folder.mkdir()
duration = 3600
active = []
stop = False

def terminate(*args):
    global stop
    stop = True

signal.signal(signal.SIGTERM, terminate)
signal.signal(signal.SIGINT, terminate)

def queue(directory, command):
    temp = base / ('danfoss-command-' + directory)
    temp.write_text(command + '\n')
    os.replace(temp, base / directory / 'danfoss-pairing')

con = sqlite3.connect(f'file:{base / "lp.db"}?mode=ro', uri=True)
cursor = con.execute('SELECT coalesce(max(rowid),0) FROM raw').fetchone()[0]
start = time.time()
metadata = {'start':start, 'end_planned':start+duration, 'raw_start_rowid':cursor,
            'purpose':'One physical spare head; record user action times separately',
            'timers_paused':active}
try:
    for timer in ('listening-post-wide.timer', 'listening-post-mt.timer'):
        if subprocess.run(['systemctl','--user','is-active','--quiet',timer]).returncode == 0:
            active.append(timer)
            subprocess.run(['systemctl','--user','stop',timer],check=True)
    (folder/'session.json').write_text(json.dumps(metadata,indent=2))
    queue('cmd', 'FSK 868.42 19.2 20 93.8 66666666 200 3600 zw96pair')
    queue('cmd-tembed', 'FSK 868.40 40 20 101 5555 64 3600 zw40pair')
    print(folder, flush=True)
    with (folder/'raw.jsonl').open('a',buffering=1) as out:
        while not stop and time.time()-start < duration:
            for rowid,ts,line in con.execute('SELECT rowid,ts,line FROM raw WHERE rowid>? ORDER BY rowid',(cursor,)).fetchall():
                out.write(json.dumps({'rowid':rowid,'ts':ts,'line':line})+'\n')
                cursor=rowid
            time.sleep(1)
finally:
    queue('cmd','STOP')
    queue('cmd-tembed','STOP')
    for timer in active:
        subprocess.run(['systemctl','--user','start',timer],check=False)
    metadata['end_actual']=time.time()
    metadata['raw_end_rowid']=cursor
    (folder/'session.json').write_text(json.dumps(metadata,indent=2))
    con.close()
