#include "../firmware-test/src/danfoss_protocol.h"
#include <stdio.h>
int main(int argc,char**argv){
 if(argc!=2)return 2;uint8_t k[16];unsigned v;
 for(int i=0;i<16;i++){sscanf(argv[1]+2*i,"%2x",&v);k[i]=v;}
 char line[2048];int checked=0;
 while(fgets(line,sizeof line,stdin)){
  size_t n=strcspn(line,"\r\n")/2;uint8_t f[1024];for(size_t i=0;i<n;i++){sscanf(line+2*i,"%2x",&v);f[i]=v;}
  uint8_t tag[4];Danfoss::mac(f+12,n-17,k,tag);if(memcmp(tag,f+n-5,4))return 3;checked++;
 }
 fprintf(stderr,"Embedded MAC verified on %d frames\n",checked);
 const char *example="ffd2bb2d10410139019100021028100c0d85030020102011030f031703280318032f033003010302030303230754006300955300a0115c617f";
 uint8_t f[57];for(int i=0;i<57;i++){sscanf(example+2*i,"%2x",&v);f[i]=v;}
 if(!Danfoss::validReport(f,57,k))return 4;
 uint8_t a[10],r[37];Danfoss::reply(f,k,10,a,r);
 for(int i=0;i<10;i++)printf("%02x",a[i]);puts("");for(int i=0;i<37;i++)printf("%02x",r[i]);puts("");
 // Reject wrong node, altered authenticated body (with valid RF checksum), and short frame.
 f[4]=15;f[56]=Danfoss::crc(f,56);if(Danfoss::validReport(f,57,k))return 5;
 f[4]=16;f[16]^=1;f[56]=Danfoss::crc(f,56);if(Danfoss::validReport(f,57,k))return 6;
 if(Danfoss::validReport(f,56,k))return 7;
 const char *shortReport="ffd2bb2d10410235019100021024110c0d46030020102011030f0317030103280318032f033007a1006300b7520008344d389ad4c2";
 uint8_t shortFrame[53];for(int i=0;i<53;i++){sscanf(shortReport+2*i,"%2x",&v);shortFrame[i]=v;}
 if(!Danfoss::validReport(shortFrame,53,k))return 8;
 Danfoss::reply(shortFrame,k,11,a,r);
 for(int i=0;i<37;i++)printf("%02x",r[i]);puts("");
 shortFrame[14]^=1;shortFrame[52]=Danfoss::crc(shortFrame,52);
 if(Danfoss::validReport(shortFrame,53,k))return 9;
 return 0;
}
