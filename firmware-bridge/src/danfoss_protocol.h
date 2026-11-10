#pragma once
// Shared Danfoss Link proprietary protocol implementation.
// Pure C++ (no Arduino dependencies) so host-side regression tests can include it.
//
// Frame layout (direct singlecast, offsets zero-based):
//   0-3 Home ID | 4 src | 5 frame control | 6 radio seq (low nibble)
//   7 total length incl. checksum | 8 dst | 9... payload | last = XOR checksum (init ff)
// Authenticated payload: 91 0002 | op | L | body | 4-byte tag
//   MAC input = op..body (starts at offset 12, length n-17); L must equal n-17
//   for the validated op families (0x10, 0x50, 0x90).
//   XTEA 32 cycles, big-endian words, CBC-MAC with zero IV, 80-then-zero padding
//   only when unaligned; tag = last 4 bytes of the final block.
#include <stdint.h>
#include <stddef.h>
#include <string.h>
namespace Danfoss {
inline uint32_t be(const uint8_t*p){return uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3];}
inline void put(uint8_t*p,uint32_t v){p[0]=v>>24;p[1]=v>>16;p[2]=v>>8;p[3]=v;}
inline void mac(const uint8_t*m,size_t n,const uint8_t*key,uint8_t*out){
 uint32_t a=0,b=0,k[4];for(int i=0;i<4;i++)k[i]=be(key+4*i);
 size_t blocks=(n+7)/8;
 for(size_t off=0;off<blocks*8;off+=8){
  uint8_t block[8]={0};for(size_t j=0;j<8;j++){size_t p=off+j;block[j]=p<n?m[p]:(p==n?0x80:0);}
  a^=be(block);b^=be(block+4);uint32_t sum=0;
  for(int i=0;i<32;i++){a+=(((b<<4)^(b>>5))+b)^(sum+k[sum&3]);sum+=0x9e3779b9;b+=(((a<<4)^(a>>5))+a)^(sum+k[(sum>>11)&3]);}
 }
 put(out,b);
}
inline uint8_t crc(const uint8_t*f,size_t n){uint8_t c=255;for(size_t i=0;i<n;i++)c^=f[i];return c;}

// Any authenticated proprietary frame on the given network, any node pair.
inline bool validFrame(const uint8_t*f,size_t n,const uint8_t*key,const uint8_t*home){
 if(n<20||n>64||f[7]!=n)return false;
 if(memcmp(f,home,4))return false;
 if((f[5]&0x8f)!=1)return false; // direct singlecast only; routed headers differ
 if(f[9]!=0x91||f[10]!=0||f[11]!=2)return false;
 if(f[12]!=0x10&&f[12]!=0x50&&f[12]!=0x90)return false;
 if(f[13]!=n-17)return false;
 if(crc(f,n-1)!=f[n-1])return false;
 uint8_t t[4];mac(f+12,n-17,key,t);return !memcmp(t,f+n-5,4);
}
// Link-layer ACK: 10 bytes, frame control 03, no MAC.
inline bool isAck(const uint8_t*f,size_t n,const uint8_t*home){
 return n==10&&f[7]==n&&!memcmp(f,home,4)&&f[5]==3&&crc(f,n-1)==f[n-1];
}
// ACK echoes the report's radio sequence; controller node id is 1.
inline size_t buildAck(const uint8_t*home,uint8_t dst,uint8_t radioSeq,uint8_t*out){
 memcpy(out,home,4);out[4]=1;out[5]=3;out[6]=radioSeq&15;out[7]=10;out[8]=dst;
 out[9]=crc(out,9);return 10;
}
// Routine CC reply: preserve the entire opaque four-byte CC context16..19.
// Genuine replies include00000101, so treating only byte19 as mode loses bits.
inline size_t buildShortReply(const uint8_t*home,uint8_t dst,uint8_t radioSeq,uint8_t appSeq,
                              uint32_t contextRaw,uint16_t setpointC100,const uint8_t*key,uint8_t*out,
                              uint8_t operation=0x10){
 if(operation!=0x10&&operation!=0x50)return 0;
 memcpy(out,home,4);out[4]=1;out[5]=0x41;out[6]=radioSeq&15;out[7]=27;out[8]=dst;
 out[9]=0x91;out[10]=0;out[11]=2;out[12]=operation;out[13]=0x0a;
 out[14]=0x08|(appSeq&7);out[15]=0x0d;put(out+16,contextRaw);
 out[20]=setpointC100>>8;out[21]=setpointC100&255;
 mac(out+12,10,key,out+22);out[26]=crc(out,26);return 27;
}
// Lossless extended codec. Preserve configuration22..31 verbatim, including its
// separate temperature word. The caller owns override policy; this codec must
// also reproduce genuine CC replies whose two leading temperatures differ.
inline size_t buildExtendedReply(const uint8_t*home,uint8_t dst,uint8_t radioSeq,uint8_t appSeq,
                                 uint32_t contextRaw,uint16_t targetC100,const uint8_t*key,uint8_t*out,
                                 const uint8_t*configuration,uint8_t operation=0x10){
 if(!configuration||targetC100<400||targetC100>2800||(operation!=0x10&&operation!=0x50))return 0;
 memcpy(out,home,4);out[4]=1;out[5]=0x41;out[6]=radioSeq&15;out[7]=37;out[8]=dst;
 out[9]=0x91;out[10]=0;out[11]=2;out[12]=operation;out[13]=0x14;
 out[14]=0x08|(appSeq&7);out[15]=0x0d;put(out+16,contextRaw);
 out[20]=targetC100>>8;out[21]=targetC100&255;
 memcpy(out+22,configuration,10);
 mac(out+12,20,key,out+32);out[36]=crc(out,36);return 37;
}
// Observation helpers (only valid on frames that passed validFrame).
enum class ReportKind { Other, Routine, Button, Alternate, Poll };
inline ReportKind reportKind(const uint8_t*f,size_t n){
 if(n==22&&f[4]>1&&f[8]==1&&f[5]==0x41&&f[12]==0x10&&f[13]==5&&
    (f[14]&0xf8)==8&&f[15]==0x0d&&f[16]==0)return ReportKind::Poll;
 if((n!=53&&n!=57)||f[4]<=1||f[4]>232||f[8]!=1||f[12]!=0x10||
    (f[14]&0xf8)!=0x10||f[15]!=0x0c)return ReportKind::Other;
 static const uint8_t routine[]={0x47,0x20,0x20,0x20,0x21,3,0,0x20,0x10,0x20,0x11,3,0x0f,3,0x17,3,0x28,3,0x18,3,0x2f,3,0x30};
 static const uint8_t button[]={0x46,3,0,0x20,0x10,0x20,0x11,3,0x0f,3,0x17,3,1,3,0x28,3,0x18,3,0x2f,3,0x30};
 static const uint8_t alternate[]={0x85,3,0,0x20,0x10,0x20,0x11,3,0x0f,3,0x17,3,0x28,3,0x18,3,0x2f,3,0x30,3,1,3,2,3,3,3,0x23};
 if(n==53&&!memcmp(f+17,button,sizeof button))return ReportKind::Button;
 if(n==57&&!memcmp(f+17,routine,sizeof routine))return ReportKind::Routine;
 if(n==57&&!memcmp(f+17,alternate,sizeof alternate))return ReportKind::Alternate;
 return ReportKind::Other;
}
inline uint16_t replySetpoint(const uint8_t*f){return uint16_t(f[20])<<8|f[21];}
inline uint32_t replyContext(const uint8_t*f){return be(f+16);}
// 53-byte local-button-change report carries the locally chosen target at n-7.
inline uint16_t reportTarget(const uint8_t*f,size_t n){return uint16_t(f[n-7])<<8|f[n-6];}
// Unresolved final status word: low-battery node 6 reports fe00. Keep raw until
// a labelled battery replacement confirms the interpretation; not a percentage.
inline uint16_t reportStatusRaw(const uint8_t*f,size_t n){
 auto kind=reportKind(f,n);
 if(kind==ReportKind::Other||kind==ReportKind::Poll)return 0;
 size_t off=n-(reportKind(f,n)==ReportKind::Button?9:7);
 return uint16_t(f[off])<<8|f[off+1];
}
// FE00 was labelled by a real low-battery replacement. Healthy heads reported
// 3400..5300 (low byte zero); codes inside that band are healthy. Anything else,
// including 3300 just below the lowest healthy observation, remains unknown.
// Hypothesis under test: the high byte of a healthy code is the battery level in
// percent (values 43..70 seen, slowly falling). -1 when the code is not of that form.
inline int8_t batteryLevel(uint16_t status){
 return (status&255)==0&&(status>>8)<=100?int8_t(status>>8):-1;
}
inline int8_t batteryLow(uint16_t status){
 if(status==0xfe00)return 1;
 if((status&255)==0&&status>=0x3400&&status<=0x5300)return 0;
 return -1;
}
}
