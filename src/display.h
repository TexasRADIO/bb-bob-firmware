/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef SW_DISPLAY_H
#define SW_DISPLAY_H
#include "swing.h"
#include "panel.h"
#include "platform.h"
typedef void (*sw_rectangle)(int x,int y,int w,int h,uint16_t colour,void *context);
void sw_display(const sw_engine *e,sw_rectangle rectangle,void *context);
void sw_panel_display(const sw_engine *e,const sw_panel *p,sw_rectangle rectangle,void *context);
void sw_platform_display(const sw_platform_stats *stats,sw_rectangle rectangle,void *context);
/* Audio-aligned mouth/tap frame plus independently advancing idle gestures. */
void sw_display_bob(const sw_engine *e,uint32_t frame,uint32_t speaking,sw_rectangle rectangle,void *context);
void sw_display_bob_at(const sw_engine *e,int x0,int y0,uint32_t frame,uint32_t speaking,sw_rectangle rectangle,void *context);
void sw_text(sw_rectangle rectangle,void *context,int x,int y,const char *s,int scale,uint16_t ink);
void sw_display_title_art(sw_rectangle rectangle,void *context,int x,int y);
#endif
