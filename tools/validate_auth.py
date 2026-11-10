#!/usr/bin/env python3
"""Validate the stored network key on archived raw windows, separating discovery/held-out data.
Usage: validate_auth.py RAW.jsonl TRAINING-FRAMES.txt (key from Keychain, see danfoss_secret.py)"""
import collections,json,sys
from pathlib import Path
from zwdecode import frames,manchester
from danfoss_auth import verify_frame
from danfoss_secret import network_key
key=network_key()
training=set(bytes.fromhex(l.strip()) for l in Path(sys.argv[2]).read_text().splitlines())
seen={};failures=[]
for line in Path(sys.argv[1]).read_text().splitlines():
 o=json.loads(line);m=o['capture'];raw=bytes.fromhex(m['hex'])
 for candidate in manchester(raw) if m['tag'].startswith('zw96') else [raw]:
  for f,_ in frames(candidate):
   if f[:4].hex()!='ffd2bb2d' or len(f)<20 or f[9:12]!=bytes.fromhex('910002') or f[12] not in (0x10,0x50,0x90):continue
   seen.setdefault(f,[]).append(o['rowid'])
for f,rows in seen.items():
 if not verify_frame(f,key):failures.append({'frame':f.hex(),'rows':rows})
new=[f for f in seen if f not in training]
new_apps=set(f[12:-1] for f in new)-set(f[12:-1] for f in training)
print(json.dumps({'unique_frames':len(seen),'occurrences':sum(map(len,seen.values())),
 'held_out_unique_frames':len(new),'held_out_unique_application_messages':len(new_apps),
 'verified':sum(verify_frame(f,key) for f in seen),'failures':failures,
 'nodes':sorted(set(n for f in seen for n in (f[4],f[8]))),
 'opcodes':dict(collections.Counter(f'{f[12]:02x}' for f in seen))},indent=2))
Path('data/auth-validated-frames.txt').write_text('\n'.join(f.hex() for f in seen)+'\n')
