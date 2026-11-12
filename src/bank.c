/* SPDX-License-Identifier: GPL-3.0-only */
#include "bank.h"
#include "bank_internal.h"
#if defined(SW_TARGET_RAM_AUDIO)&&!defined(__APPLE__)
#define SW_RAM_AUDIO __attribute__((section(".ram_audio")))
#else
#define SW_RAM_AUDIO
#endif
static uint32_t u16(const uint8_t *p) {return p[0]|((uint32_t)p[1]<<8);}
static uint32_t u32(const uint8_t *p) {return u16(p)|(u16(p+2)<<16);}
SW_RAM_AUDIO static uint32_t u16_audio(const uint8_t *p) {return p[0]|((uint32_t)p[1]<<8);}
SW_RAM_AUDIO static uint32_t u32_audio(const uint8_t *p) {return u16_audio(p)|(u16_audio(p+2)<<16);}
static uint32_t crc(const uint8_t *p,uint32_t n,uint32_t skip,void (*poll)(void)) {
 uint32_t c=0xffffffffu;
 for(uint32_t i=0;i<n;i++) {if(poll&&!(i&1023u))poll();c^=(i>=skip&&i-skip<4u)?0u:p[i];for(uint32_t j=0;j<8;j++)c=(c>>1)^(0xedb88320u&(0u-(c&1u)));}
 return c^0xffffffffu;
}
static int string_ok(const sw_bank *b,uint32_t off,uint32_t max) {
 if(off<SW_BANK_HEADER+b->count*SW_BANK_ENTRY||off>=b->length)return 0;
 for(uint32_t i=0;i<max&&i<b->length-off;i++) {uint8_t c=b->bytes[off+i];if(!c)return i>0;if(c<32||c>126)return 0;}
 return 0;
}
SW_RAM_AUDIO int sw_bank_audio(const sw_bank *b,uint32_t index,sw_bank_clip *out) {
 if(!b||!b->bytes||!out||index>=b->count)return 0;
 uint32_t kind=u16_audio(b->bytes+SW_BANK_HEADER+index*SW_BANK_ENTRY);
 if(kind!=SW_BANK_CALL&&kind!=SW_BANK_FEATURE&&kind!=SW_BANK_BACKING)return 0;
 const uint8_t *p=b->bytes+SW_BANK_HEADER+index*SW_BANK_ENTRY;
 out->flags=u32_audio(p+4);out->length=u32_audio(p+20);out->samples=u32_audio(p+24);out->rate=u32_audio(p+28);
 out->initial=(int32_t)u16_audio(p+32);if(out->initial>=32768)out->initial-=65536;
 out->step_index=p[34];out->targets=u32_audio(p+44);
 out->pcm=b->bytes+u32_audio(p+16);out->label=(const char *)b->bytes+u32_audio(p+36);out->short_label=(const char *)b->bytes+u32_audio(p+40);return 1;
}
SW_RAM_AUDIO int sw_bank_call(const sw_bank *b,uint32_t index,sw_bank_clip *out) {
 if(!b||index>=b->calls)return 0;
 return sw_bank_audio(b,index,out);
}
static int art_profile(const uint8_t *p,uint32_t n) {
 static const uint8_t tag[8]={'B','B','A','R','T','0','1',0};
 if(!p||n<48)return 0;
 for(uint32_t i=0;i<8;i++)if(p[40+i]!=tag[i])return 0;
 return 1;
}
int sw_bank_open_art_envelope(sw_bank *out,const uint8_t *p,uint32_t n,uint32_t limit,void (*poll)(void)) {
 static const uint8_t magic[8]={'B','B','B','O','B','0','1',0};
 if(!out)return 0;
 out->bytes=0;out->length=out->count=out->calls=0;
 if(!p||n<SW_BANK_HEADER||n>limit||n>SW_BANK_LIMIT)return 0;
 for(uint32_t i=0;i<8;i++)if(p[i]!=magic[i])return 0;
 uint32_t count=u32(p+28);
 if(u16(p+8)!=1||u16(p+10)!=SW_BANK_HEADER||u32(p+12)!=n||u32(p+24)!=SW_BANK_HEADER||u32(p+32)!=SW_BANK_ENTRY||u32(p+36)!=1||!count||count>1024u)return 0;
 for(uint32_t i=56;i<64;i++)if(p[i])return 0;
 if(count>(n-SW_BANK_HEADER)/SW_BANK_ENTRY||crc(p,SW_BANK_HEADER,20,poll)!=u32(p+20)||crc(p+SW_BANK_HEADER,n-SW_BANK_HEADER,0xffffffffu,poll)!=u32(p+16))return 0;
 sw_bank b={p,n,count,0};
 for(uint32_t i=0;i<count;i++) {
  const uint8_t *q=p+SW_BANK_HEADER+i*SW_BANK_ENTRY;
  uint32_t off=u32(q+16),len=u32(q+20),frames=u32(q+24),rate=u32(q+28);
  uint32_t kind=u16(q),codec=u16(q+2);
  if(off<SW_BANK_HEADER+count*SW_BANK_ENTRY||off>n||len>n-off||!string_ok(&b,u32(q+36),129)||!string_ok(&b,u32(q+40),17))return 0;
  if(kind==SW_BANK_CALL||kind==SW_BANK_FEATURE||kind==SW_BANK_BACKING) {
   if(kind==SW_BANK_CALL&&(i!=b.calls||++b.calls>384u))return 0;
   if(codec!=SW_BANK_IMA||!frames||rate<3000u||rate>22050u||frames>0xffffeu||frames>rate*60u||q[34]>88||q[35]||u32(q+4)&~511u||len!=frames/2u)return 0;
  } else if(kind==SW_BANK_PCM8) {
   if(codec!=SW_BANK_CODEC_PCM8||len!=frames||!frames||rate<3000u||rate>44100u||u16(q+32)>127||q[34]!=1||q[35])return 0;
  } else if(kind==SW_BANK_ART||kind==SW_BANK_GLYPHS) {
   uint32_t w=u16(q+32),h=u16(q+34);
   if((codec!=SW_BANK_RAW&&codec!=SW_BANK_LZ4&&!(kind==SW_BANK_ART&&art_profile(p,n)&&(codec==SW_BANK_RLE5||codec==SW_BANK_LAYERS)))||!w||!h||w>240u||h>240u||frames!=w*h||rate||!len||(codec==SW_BANK_RAW&&len!=frames))return 0;
  } else if(kind==SW_BANK_PALETTE) {
   if(codec!=SW_BANK_RAW||len!=64||frames!=32||rate||u32(q+32))return 0;
  } else return 0;
  for(uint32_t j=0;j<i;j++) {const uint8_t *r=p+SW_BANK_HEADER+j*SW_BANK_ENTRY;if(u32(q+8)==u32(r+8)&&u32(q+12)==u32(r+12))return 0;}
 }
 *out=b;return 1;
}

int sw_bank_find(const sw_bank *b,uint16_t kind,uint32_t low,uint32_t high,sw_bank_asset *out) {
 if(!b||!b->bytes||!out)return 0;
 for(uint32_t i=0;i<b->count;i++) {const uint8_t *p=b->bytes+SW_BANK_HEADER+i*SW_BANK_ENTRY;
  if(u16(p)!=kind||u32(p+8)!=low||u32(p+12)!=high)continue;
  out->kind=(uint16_t)u16(p);out->codec=(uint16_t)u16(p+2);out->flags=u32(p+4);out->data=b->bytes+u32(p+16);out->length=u32(p+20);out->decoded=u32(p+24);out->rate=u32(p+28);out->aux=u32(p+32);out->label=(const char *)b->bytes+u32(p+36);return 1;
 }return 0;
}

int sw_bank_open_polled(sw_bank *out,const uint8_t *p,uint32_t n,uint32_t limit,void (*poll)(void)) {
 if(art_profile(p,n)){if(out){out->bytes=0;out->length=out->count=out->calls=0;}return 0;}
 return sw_bank_open_art_envelope(out,p,n,limit,poll);
}
int sw_bank_open(sw_bank *out,const uint8_t *p,uint32_t n,uint32_t limit) {return sw_bank_open_polled(out,p,n,limit,0);}
