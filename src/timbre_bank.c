/* SPDX-License-Identifier: GPL-3.0-only */
#include "timbre_bank.h"
#include "timbre_bank_data.h"
#if defined(SW_TARGET_RAM_AUDIO)&&!defined(__APPLE__)
#define SW_RAM_AUDIO __attribute__((section(".ram_audio")))
#else
#define SW_RAM_AUDIO
#endif
#ifdef __APPLE__
#define SW_TB_POOL __attribute__((section("__DATA,__tb_cache"),aligned(4)))
#else
#define SW_TB_POOL __attribute__((section(".pool"),aligned(4)))
#endif
static uint8_t cache[SW_TB_PCM_BYTES] SW_TB_POOL;
static int status;
const uint8_t (*sw_fiddle_pcm)[7200];
const uint8_t (*sw_fiddle_short_pcm)[3600];
const uint8_t (*sw_upright_pcm)[4800];
const uint8_t (*sw_piano_pcm)[6000];

static uint32_t word(const uint8_t *p) {
 return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static uint32_t crc(const uint8_t *p,uint32_t n,void (*watchdog)(void)) {
 uint32_t value=~0u;
 for(uint32_t i=0;i<n;i++) {
  value^=p[i];
  for(uint32_t bit=0;bit<8;bit++)value=(value>>1)^(0xedb88320u&(0u-(value&1u)));
  if(watchdog&&!(i&1023u))watchdog();
 }
 return ~value;
}
int sw_timbre_bank_status(void) {return status;}
SW_RAM_AUDIO int sw_timbre_bank_status_audio(void) {return status;}
int sw_timbre_bank_boot(const void *image,uint32_t bytes,void (*watchdog)(void)) {
 const uint8_t *p=(const uint8_t *)image;
 sw_fiddle_pcm=0;sw_fiddle_short_pcm=0;sw_upright_pcm=0;sw_piano_pcm=0;
 status=SW_TB_MISSING;
 if(!p)return status;
 if(bytes!=SW_TB_SLOT_BYTES)return status=SW_TB_SIZE;
 if(word(p)!=0x42544242u||word(p+4)!=1u||word(p+8)!=SW_TB_HEADER_BYTES+SW_TB_PCM_BYTES||
    word(p+12)!=SW_TB_PAYLOAD_CRC||word(p+16)!=4u||word(p+20)!=SW_TB_PCM_BYTES)return status=SW_TB_FORMAT;
 /* The fingerprint covers exact IDs, nonoverlapping offsets, lengths, CRCs
  * and reserved bytes. Roots, rates, decode and loop metadata remain trusted
  * compiled constants; this bank supplies no mutable pitch/loop parameters. */
 if(crc(p,SW_TB_HEADER_BYTES,watchdog)!=SW_TB_HEADER_CRC)return status=SW_TB_HEADER;
 if(crc(p+SW_TB_HEADER_BYTES,SW_TB_PCM_BYTES,watchdog)!=SW_TB_PAYLOAD_CRC)return status=SW_TB_CONTENT;
 for(uint32_t i=0;i<SW_TB_PCM_BYTES;i++) {
  cache[i]=p[SW_TB_HEADER_BYTES+i];
  if(watchdog&&!(i&1023u))watchdog();
 }
 /* Also reject a source changed during the bounded startup copy. */
 if(crc(cache,SW_TB_PCM_BYTES,watchdog)!=SW_TB_PAYLOAD_CRC)return status=SW_TB_COPY;
 sw_fiddle_pcm=(const uint8_t (*)[7200])cache;
 sw_fiddle_short_pcm=(const uint8_t (*)[3600])(cache+28800);
 sw_upright_pcm=(const uint8_t (*)[4800])(cache+57600);
 sw_piano_pcm=(const uint8_t (*)[6000])(cache+67200);
 return status=SW_TB_READY;
}
