/* SPDX-License-Identifier: GPL-3.0-only
 * BB-BOB bank v1: byte-addressed, little-endian, read-only after validation.
 * The caller owns immutable storage for the view's entire playback lifetime.
 * No allocation, flash writes or hardware access occur here. */
#ifndef BB_BOB_BANK_H
#define BB_BOB_BANK_H
#include <stdint.h>
/* Stable target-bit order; new names append, existing bits never move. */
enum { SW_CHAR_BOB, SW_CHAR_LEON, SW_CHAR_ELDON, SW_CHAR_AL, SW_CHAR_TINY,
 SW_CHAR_HERB, SW_CHAR_TOMMY, SW_CHAR_JUNIOR, SW_CHAR_NOEL, SW_CHAR_JIMMY,
 SW_CHAR_JESSE, SW_CHAR_JOAQUIN, SW_CHAR_LOUIE, SW_CHAR_JOHNNY,
 SW_CHAR_BROTHER_JOHN, SW_CHAR_PEEWEE, SW_CHAR_SLEEPY, SW_CHAR_CHARLIE,
 SW_CHAR_SMOKEY, SW_CHAR_KELSO };
#define SW_BANK_HEADER 64u
#define SW_BANK_ENTRY 48u
#define SW_BANK_CALL 1u
#define SW_BANK_PCM8 2u
#define SW_BANK_ART 3u
#define SW_BANK_PALETTE 4u
#define SW_BANK_GLYPHS 5u
#define SW_BANK_FEATURE 6u
#define SW_BANK_BACKING 7u
#define SW_BANK_IMA 1u
#define SW_BANK_CODEC_PCM8 2u
#define SW_BANK_LZ4 3u
#define SW_BANK_RAW 4u
#define SW_BANK_RLE5 5u
#define SW_BANK_LAYERS 6u
#define SW_BANK_LIMIT (16u*1024u*1024u)
typedef struct { const uint8_t *bytes; uint32_t length,count,calls; } sw_bank;
typedef struct {
 const uint8_t *pcm; const char *label,*short_label;
 uint32_t length,samples,rate,flags,targets; int32_t initial; uint32_t step_index;
} sw_bank_clip;
/* Failure clears out. limit is the caller's storage window, not an entitlement
 * to a larger physical bank. CRC validation is boot/load work, never ISR work.
 * BBART01 is deliberately refused: use sw_art_bank_open for that profile. */
int sw_bank_open(sw_bank *out,const uint8_t *bytes,uint32_t length,uint32_t limit);
int sw_bank_open_polled(sw_bank *out,const uint8_t *bytes,uint32_t length,uint32_t limit,void (*poll)(void));
typedef struct { const uint8_t *data; const char *label; uint32_t length,decoded,rate,aux,flags; uint16_t kind,codec; } sw_bank_asset;
/* Lookup is load/display setup work. Art codecs are opaque here: a consumer
 * must separately bound/decode LZ4 output before displaying it. */
int sw_bank_find(const sw_bank *bank,uint16_t kind,uint32_t id_low,uint32_t id_high,sw_bank_asset *out);
int sw_bank_audio(const sw_bank *bank,uint32_t index,sw_bank_clip *out);
int sw_bank_call(const sw_bank *bank,uint32_t index,sw_bank_clip *out);
#endif
