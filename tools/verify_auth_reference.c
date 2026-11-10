/* Independent offline reference for captured Danfoss tags. No radio access.
 * Usage: compile, then pass a 32-hex-digit key; stdin = full frames as hex lines.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static uint32_t be(const unsigned char *p) {return (uint32_t)p[0]<<24 | (uint32_t)p[1]<<16 | (uint32_t)p[2]<<8 | p[3];}
static void enc(uint32_t *a, uint32_t *b, const uint32_t *k) {
 uint32_t sum=0;
 for (int i=0;i<32;i++) {
  *a += (((*b<<4 ^ *b>>5) + *b) ^ (sum + k[sum&3]));
  sum += UINT32_C(0x9e3779b9);
  *b += (((*a<<4 ^ *a>>5) + *a) ^ (sum + k[(sum>>11)&3]));
 }
}
int main(int argc,char **argv) {
 if(argc!=2 || strlen(argv[1])!=32)return 2;
 unsigned char kb[16];unsigned x;uint32_t k[4];
 for(int i=0;i<16;i++){if(sscanf(argv[1]+2*i,"%2x",&x)!=1)return 2;kb[i]=x;}
 for(int i=0;i<4;i++)k[i]=be(kb+4*i);
 char line[2048];int pass=0,fail=0;
 while(fgets(line,sizeof line,stdin)){
  size_t n=strcspn(line,"\r\n")/2;unsigned char f[1024],m[1024]={0};
  if(n<20 || n>255)return 2;
  for(size_t i=0;i<n;i++){if(sscanf(line+2*i,"%2x",&x)!=1)return 2;f[i]=x;}
  size_t len=n-17,padded=len;memcpy(m,f+12,len);
  if(len%8){m[len]=128;padded=(len+7)/8*8;}
  uint32_t a=0,b=0;
  for(size_t i=0;i<padded;i+=8){a^=be(m+i);b^=be(m+i+4);enc(&a,&b,k);}
  if(b==be(f+n-5))pass++;else fail++;
 }
 printf("Independent C reference: %d matched, %d failed\n",pass,fail);
 return fail?1:0;
}
