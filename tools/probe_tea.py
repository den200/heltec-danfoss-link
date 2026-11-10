#!/usr/bin/env python3
"""Test TEA/XTEA CBC-MAC hypotheses offline; no claim these are Danfoss algorithms."""
import struct,sys,collections,json
from probe_auth import KEY,swap
MASK=0xffffffff

def block(key,data,kind,endian):
 v0,v1=struct.unpack(endian+'2I',data); k=struct.unpack(endian+'4I',key);s=0
 for _ in range(32):
  if kind=='tea':
   s=(s+0x9e3779b9)&MASK
   v0=(v0+(((v1<<4)+k[0])^(v1+s)^((v1>>5)+k[1])))&MASK
   v1=(v1+(((v0<<4)+k[2])^(v0+s)^((v0>>5)+k[3])))&MASK
  else:
   v0=(v0+((((v1<<4)^(v1>>5))+v1)^(s+k[s&3])))&MASK
   s=(s+0x9e3779b9)&MASK
   v1=(v1+((((v0<<4)^(v0>>5))+v0)^(s+k[(s>>11)&3])))&MASK
 return struct.pack(endian+'2I',v0,v1)

def mac(key,msg,kind,endian,pad):
 if pad=='iso':msg+=b'\x80';msg+=bytes(-len(msg)%8)
 elif pad=='pkcs':n=8-len(msg)%8;msg+=bytes([n])*n
 else:msg+=bytes(-len(msg)%8)
 state=bytes(8)
 for i in range(0,len(msg),8):state=block(key,bytes(a^b for a,b in zip(state,msg[i:i+8])),kind,endian)
 return state

def main():
 assert block(bytes(16),bytes(8),'tea','>').hex()=='41ea3a0a94baa940'
 assert block(bytes(16),bytes(8),'xtea','>').hex()=='dee9d4d8f7131ed9'
 groups=collections.defaultdict(dict)
 for l in open(sys.argv[1]):
  f=bytes.fromhex(l.strip());groups[(f[12],f[13])][f[14:-1]]=f
 count=0;matches=[]
 for g,d in groups.items():
  fs=list(d.values())
  if len(fs)<3:continue
  for start in (9,10,12,13,14,15,16):
   for keyname,key in [('raw',KEY),('reverse',KEY[::-1]),('swap16',swap(KEY,2)),('swap32',swap(KEY,4)),('zero',bytes(16))]:
    for kind in ('tea','xtea'):
     for endian in ('<','>'):
      for pad in ('zero','iso','pkcs'):
       count+=1
       def digest(f):return mac(key,f[start:-5],kind,endian,pad)
       m=digest(fs[0]);tag=fs[0][-5:-1]
       for rev in (False,True):
        for off in range(5):
         if m[off:off+4]!=(tag[::-1] if rev else tag):continue
         if all(digest(f)[off:off+4]==(f[-5:-1][::-1] if rev else f[-5:-1]) for f in fs[1:]):matches.append([g,start,keyname,kind,endian,pad,rev,off])
 print(json.dumps({'known_answer_tests':'passed','rules_tested':count,'matches':matches},indent=2))
if __name__=='__main__':main()
