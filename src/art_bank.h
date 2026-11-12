/* SPDX-License-Identifier: GPL-3.0-only; immutable BBART01 bank profile. */
#ifndef SW_ART_BANK_H
#define SW_ART_BANK_H
#include "bank.h"
#include "showbill.h"
#define SW_ART_BANK_BYTES 81920u
#define SW_ART_BANK_OFFSET 0xe8000u
#define SW_ART_IMAGES 32u
typedef struct {sw_bank bank;sb_art art;sb_image_t images[SW_ART_IMAGES];} sw_art_bank;
/* Boot/load foreground only. Failure clears out; caller detaches old art before
 * replacing its storage. All bytes remain immutable for the view's lifetime. */
uint32_t sw_art_bank_context_bytes(void);
int sw_art_bank_open(sw_art_bank *out,const uint8_t *bytes,uint32_t length,void (*poll)(void));
/* Drawing is bounded to one plane or three layers, no recursion/allocation. */
void sw_art_bank_blit(const sb_image_t *image,sw_rectangle rect,void *context,int x,int y,int scale);
#endif
