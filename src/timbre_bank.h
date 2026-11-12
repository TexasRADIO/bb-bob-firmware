/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef SW_TIMBRE_BANK_H
#define SW_TIMBRE_BANK_H
#include <stdint.h>
#define SW_TB_SLOT_BYTES 81920u
#define SW_TB_HEADER_BYTES 512u
#define SW_TB_PCM_BYTES 79200u
#define SW_TB_FLASH_OFFSET 0xA0000u
enum {SW_TB_MISSING=0,SW_TB_READY=1,SW_TB_SIZE=-1,SW_TB_FORMAT=-2,
      SW_TB_HEADER=-3,SW_TB_CONTENT=-4,SW_TB_COPY=-5};
/* Startup only, before any engine renders. No hot reload, allocation, device
 * writes or file I/O. Source must remain readable for exactly SLOT_BYTES until
 * this returns. The validated bytes are copied into an immutable pool cache.
 * A failed load clears every published pointer; the engine uses synth fallback.
 * The optional watchdog hook must not reenter this API or start audio. */
int sw_timbre_bank_boot(const void *image,uint32_t bytes,void (*watchdog)(void));
int sw_timbre_bank_status(void);
int sw_timbre_bank_status_audio(void);
/* These pointers remain private to the sample headers/renderer after boot. */
extern const uint8_t (*sw_fiddle_pcm)[7200];
extern const uint8_t (*sw_fiddle_short_pcm)[3600];
extern const uint8_t (*sw_upright_pcm)[4800];
extern const uint8_t (*sw_piano_pcm)[6000];
#endif
