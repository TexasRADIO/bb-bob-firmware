/* SPDX-License-Identifier: GPL-3.0-only; original bounded SHA-256 identity reader.
 * Runs before audio starts. No digest is embedded in its own input image. */
#include "bob_identity.h"
#include <stdint.h>
char sw_bob_app_sha_prefix[9]="HOST";
#ifdef SW_BOB_FULL_SHA
char sw_bob_app_sha_full[65]="HOST";
#endif
static uint32_t rotate(uint32_t n,unsigned b) {return (n>>b)|(n<<(32-b));}
void sw_bob_hash_application(const unsigned char *data,unsigned int length,void (*feed)(void)) {
 static const uint32_t k[64]={
  0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
  0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
  0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
  0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
  0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
  0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
  0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
  0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
 uint32_t h[8]={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
 /* Firmware images are <=0x8dfbc; reject impossible lengths before padding. */
 if(length>0x8dfbcu)return;
 unsigned int total=(length+72u)&~63u;
 for(unsigned int offset=0;offset<total;offset+=64) {
  uint32_t w[64];
  for(unsigned i=0;i<16;i++) {
   uint32_t v=0;
   for(unsigned j=0;j<4;j++) {
    unsigned pos=offset+i*4+j;uint32_t byte=0;
    if(pos<length)byte=data[pos];else if(pos==length)byte=128;
    else if(pos>=total-4)byte=(length*8u)>>((total-1-pos)*8u)&255u;
    v=(v<<8)|byte;
   }w[i]=v;
  }
  for(unsigned i=16;i<64;i++) {
   uint32_t a=w[i-15],b=w[i-2];
   w[i]=w[i-16]+(rotate(a,7)^rotate(a,18)^(a>>3))+w[i-7]+(rotate(b,17)^rotate(b,19)^(b>>10));
  }
  uint32_t a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],hh=h[7];
  for(unsigned i=0;i<64;i++) {
   uint32_t t1=hh+(rotate(e,6)^rotate(e,11)^rotate(e,25))+((e&f)^(~e&g))+k[i]+w[i];
   uint32_t t2=(rotate(a,2)^rotate(a,13)^rotate(a,22))+((a&b)^(a&c)^(b&c));
   hh=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;
  }
  h[0]+=a;h[1]+=b;h[2]+=c;h[3]+=d;h[4]+=e;h[5]+=f;h[6]+=g;h[7]+=hh;
  if(feed)feed();
 }
 static const char digits[]="0123456789ABCDEF";
 for(unsigned i=0;i<8;i++)sw_bob_app_sha_prefix[i]=digits[(h[0]>>(28-i*4))&15u];
 sw_bob_app_sha_prefix[8]=0;
 #ifdef SW_BOB_FULL_SHA
 /* Base machine: the whole SHA-256 for exact runtime identity over USB (SysEx IDENTITY). */
 for(unsigned w=0;w<8;w++)for(unsigned i=0;i<8;i++)sw_bob_app_sha_full[w*8+i]=digits[(h[w]>>(28-i*4))&15u];
 sw_bob_app_sha_full[64]=0;
 #endif
}
