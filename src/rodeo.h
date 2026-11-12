/* SPDX-License-Identifier: GPL-3.0-only
 * Rodeo headings (Taylor Rushing's Rodeo, used characters only, 1 bpp, cap height RODEO_H px).
 * Lower case draws as upper case; any character outside the atlas draws as a space. */
#ifndef SW_RODEO_H
#define SW_RODEO_H
#include <stdint.h>
#include "display.h"
int sw_rodeo_width(const char *s, int scale);
/* Draws at (x, y) top-left, each pixel scale x scale; returns the width drawn. Rectangles are clipped to the240px display. */
int sw_rodeo_text(sw_rectangle r, void *c, int x, int y, const char *s, int scale, uint16_t ink);
uint32_t sw_rodeo_height(void);
/* The song key / chord root in large Rodeo (cap height RODEO_KEY_H): pitch class 0-11 as C C# D Eb E F F# G Ab A Bb B,
 * the accidental raised after the letter as chord symbols are written. Returns the width drawn. */
int sw_rodeo_key(sw_rectangle r, void *c, int x, int y, uint32_t pitch_class, uint16_t ink);
int sw_rodeo_key_width(uint32_t pitch_class);
/* Compact controls reuse the same letter atlas at cap heights9..26; invalid sizes return0. */
int sw_rodeo_key_size(sw_rectangle r,void *c,int x,int y,uint32_t pitch_class,int cap_height,uint16_t ink);
int sw_rodeo_key_width_size(uint32_t pitch_class,int cap_height);
#endif
