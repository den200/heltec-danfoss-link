#!/usr/bin/env python3
"""Build (never transmit) a 28 C node-16 command from the captured 17 C command."""
import json
from pathlib import Path
from danfoss_auth import tag,verify_frame
root=Path(__file__).resolve().parents[1]
k=bytes.fromhex(json.loads((root/'data/network-key.json').read_text())['authentication_key'])
f=bytearray.fromhex('ffd2bb2d0141041b10910002100a0e0d0000000006a4904fe4b3ab')
assert verify_frame(f,k)
f[6]=5
f[14]=0x0f
f[20:22]=(2800).to_bytes(2,'big')
f[-5:-1]=tag(bytes(f[12:-5]),k)
c=0xff
for b in f[:-1]:c^=b
f[-1]=c
assert verify_frame(f,k)
result={'home_id':f[:4].hex(),'source':f[4],'destination':f[8],'setpoint_c':28,'radio_sequence':f[6],
 'application_sequence_byte':f[14],'frame':f.hex(),'status':'prepared; no transmission by this script',
 'basis':'Last captured node-16 short command; sequence advanced by one. Acceptance not established.'}
(root/'data/test-command-28C.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result,indent=2))
