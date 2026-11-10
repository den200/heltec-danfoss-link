#!/usr/bin/env python3
"""Bounded offline candidate-key tests. No RF transmission or key brute force.
Tests each message family independently; requires >=3 distinct application messages.
Reports matching rules only after validating every distinct message in that family.
"""
import collections, hashlib, hmac, json, sys
from cryptography.hazmat.primitives.cmac import CMAC
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes

from danfoss_secret import network_key
KEY=network_key()
def swap(b,n):return b''.join(b[i:i+n][::-1] for i in range(0,len(b),n))
def aes(k,b,decrypt=False):
 c=Cipher(algorithms.AES(k),modes.ECB())
 e=c.decryptor() if decrypt else c.encryptor()
 return e.update(b)+e.finalize()
def cmac(k,m):
 c=CMAC(algorithms.AES(k));c.update(m);return c.finalize()
def cbc(k,m,pad):
 if pad=='iso':m+=b'\x80';m+=bytes(-len(m)%16)
 elif pad=='pkcs':n=16-len(m)%16;m+=bytes([n])*n
 else:m+=bytes(-len(m)%16)
 if not m:m=bytes(16)
 e=Cipher(algorithms.AES(k),modes.CBC(bytes(16))).encryptor()
 return (e.update(m)+e.finalize())[-16:]
def run(path):
 frames=[bytes.fromhex(l.strip()) for l in open(path) if l.strip()]
 groups=collections.defaultdict(dict)
 for f in frames:
  assert f[7]==len(f)
  groups[(f[12],f[13])][f[14:-1]]=f
 keys={'raw':KEY,'reverse':KEY[::-1],'swap16':swap(KEY,2),'swap32':swap(KEY,4),'zero':bytes(16)}
 for c in (0,0x55,0xaa,0xff):keys[f'AES-constant-{c:02x}']=aes(KEY,bytes([c])*16)
 keys['decrypt-under-zero']=aes(bytes(16),KEY,True)
 algorithms_={'CMAC':cmac}
 for p in ('zero','iso','pkcs'):algorithms_['CBC-'+p]=lambda k,m,p=p:cbc(k,m,p)
 for name in ('md5','sha1','sha256'):
  algorithms_['HMAC-'+name]=lambda k,m,name=name:hmac.new(k,m,name).digest()
  algorithms_['prefix-'+name]=lambda k,m,name=name:hashlib.new(name,k+m).digest()
  algorithms_['suffix-'+name]=lambda k,m,name=name:hashlib.new(name,m+k).digest()
  algorithms_['plain-'+name]=lambda k,m,name=name:hashlib.new(name,m).digest()
 matches=[]; trials=0
 for group,unique in sorted(groups.items()):
  fs=list(unique.values())
  if len(fs)<3:continue
  print('family',group,'distinct messages',len(fs),flush=True)
  # Full frame, application header, vendor header or body; no presumed tag bytes in message.
  for start in (0,4,8,9,10,12,13,14,15,16):
   for transform in ('raw','reverse','swap16','swap32','length-prefix'):
    def message(f):
     m=f[start:-5]
     if transform=='reverse':return m[::-1]
     if transform=='swap16':return swap(m,2)
     if transform=='swap32':return swap(m,4)
     if transform=='length-prefix':return bytes([len(m)])+m
     return m
    for kn,k in keys.items():
     for an,fn in algorithms_.items():
      trials+=1
      digest=fn(k,message(fs[0]));tag=fs[0][-5:-1]
      for reverse_tag in (False,True):
       target=tag[::-1] if reverse_tag else tag
       for offset in range(len(digest)-3):
        if digest[offset:offset+4]!=target:continue
        if all(fn(k,message(f))[offset:offset+4]==(f[-5:-1][::-1] if reverse_tag else f[-5:-1]) for f in fs[1:]):
         matches.append(dict(family=group,key=kn,algorithm=an,start=start,transform=transform,offset=offset,reverse_tag=reverse_tag,validated=len(fs)))
         print('MATCH',matches[-1],flush=True)
 result={'candidate_rules_tested':trials,'matches':matches,'frames':len(frames),'key_variants':list(keys)}
 print(json.dumps(result,indent=2))
if __name__=='__main__':run(sys.argv[1])
