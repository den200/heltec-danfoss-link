#pragma once
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
inline bool validReport(const uint8_t*f,size_t n,const uint8_t*k){
 const uint8_t head[]={0xff,0xd2,0xbb,0x2d,16,0x41};
 if((n!=57 && n!=53)||memcmp(f,head,6)||f[7]!=n||f[8]!=1||f[9]!=0x91||f[10]!=0||f[11]!=2||f[12]!=0x10||f[13]!=(n-17)||f[15]!=0x0c||crc(f,n-1)!=f[n-1])return false;
 uint8_t t[4];mac(f+12,n-17,k,t);return !memcmp(t,f+n-5,4);
}
inline void reply(const uint8_t*report,const uint8_t*key,uint8_t radioSeq,uint8_t*ack,uint8_t*response){
 const uint8_t a[]={0xff,0xd2,0xbb,0x2d,1,3,0,10,16,0};memcpy(ack,a,10);ack[6]=report[6]&15;ack[9]=crc(ack,9);
 // Match the extended CC configuration observed for this head, with both setpoints 28 C.
 const uint8_t r[]={0xff,0xd2,0xbb,0x2d,1,0x41,0,37,16,0x91,0,2,0x10,0x14,
  8,0x0d,0,0,0,3,0x0a,0xf0,0x0a,0xf0,2,0x58,0x0a,0xf0,0,0,0x0a,0xf0,0,0,0,0,0};
 memcpy(response,r,37);response[6]=radioSeq&15;response[14]=8|(report[14]&7);
 mac(response+12,20,key,response+32);response[36]=crc(response,36);
}
}
