/* SPDX-License-Identifier: GPL-3.0-only; original bounded platform telemetry. */
#ifndef SW_PLATFORM_H
#define SW_PLATFORM_H
#include <stdint.h>
#ifdef SW_RENDER_PROFILE
#include "render_profile.h"
#endif
#ifndef SW_HALF_FRAMES
#define SW_HALF_FRAMES 128u
#endif
#if SW_HALF_FRAMES != 128u && SW_HALF_FRAMES != 256u
#error Unsupported audio half size
#endif
#define SW_TIMER_HZ 24000000u
/* Floor is deliberately conservative by less than one timer tick. */
#define SW_HALF_TICKS ((uint32_t)((uint64_t)SW_HALF_FRAMES * SW_TIMER_HZ / 44100u))
#define SW_STACK_PAINT 0x5ba3c69du
#define SW_DIAGNOSTIC_BUTTONS ((1u<<4)|(1u<<5)) /* ENV + LFO; hold to view. */
typedef struct {
 uint32_t halves,peak_ticks,over_budget,dma_misses,entry_peak_ticks;
 uint32_t previous_entry,have_entry,mask_peak_ticks,user_stack,irq_stack;
 #ifdef SW_DIAGNOSTICS
 uint32_t run,run_started_ms,lcd_disabled,lcd_pending,profile_enabled;
 uint32_t audio_total_ticks;
 uint32_t timer5_body_peak,timer5_entry_peak,timer5_previous_entry,timer5_have_entry;
 uint32_t scan_total_ticks,scan_half_peak,scan_tick_peak,scan_gap_peak,scan_on_peak;
 uint32_t render_peak,convert_peak,voices_peak,control_peak,voice_peak,mix_peak,note_peak,starts_peak;
 uint32_t draw_peak,blit_peak,frozen_halves;
 #ifdef SW_RENDER_PROFILE
 uint64_t profile_total[SW_PROFILE_SCOPES];
 uint32_t profile_peak[SW_PROFILE_SCOPES];
 #endif
 #endif
} sw_platform_stats;
static inline void sw_platform_half(volatile sw_platform_stats *s,uint32_t entry,uint32_t elapsed,uint32_t missed) {
 if(s->have_entry) {uint32_t gap=entry-s->previous_entry;if(gap>s->entry_peak_ticks)s->entry_peak_ticks=gap;}
 s->previous_entry=entry;s->have_entry=1;
 if(elapsed>s->peak_ticks)s->peak_ticks=elapsed;
 if(elapsed>=SW_HALF_TICKS)s->over_budget++;
 if(missed)s->dma_misses++;
 s->halves++;
 #ifdef SW_DIAGNOSTICS
 s->audio_total_ticks+=elapsed;
 if(s->lcd_disabled&&!s->lcd_pending)s->frozen_halves++;
 #endif
}
#ifdef SW_DIAGNOSTICS
/* TIMER5 is lower priority than audio. Subtract any higher-priority audio span
   from its wall time so this reports TIMER5's own scan/USB/UART body. */
static inline void sw_platform_timer5(volatile sw_platform_stats *s,uint32_t entry,uint32_t body) {
 if(s->timer5_have_entry) {uint32_t gap=entry-s->timer5_previous_entry;if(gap>s->timer5_entry_peak)s->timer5_entry_peak=gap;}
 s->timer5_previous_entry=entry;s->timer5_have_entry=1;
 if(body>s->timer5_body_peak)s->timer5_body_peak=body;
}
/* Foreground only, with audio/scan IRQs serialized by the adapter. Stack paint
   cannot be restarted safely here: retain conservative boot high-water marks. */
static inline void sw_platform_reset(volatile sw_platform_stats *s,uint32_t now) {
 uint32_t run=s->run+1u,user=s->user_stack,irq=s->irq_stack,lcd=s->lcd_disabled,pending=s->lcd_pending,profile=s->profile_enabled;
 volatile uint8_t *p=(volatile uint8_t *)s;
 for(uint32_t i=0;i<sizeof(*s);i++)p[i]=0;
 s->run=run;s->run_started_ms=now;s->user_stack=user;s->irq_stack=irq;
 s->lcd_disabled=lcd;s->lcd_pending=pending;s->profile_enabled=profile;
}
static inline void sw_platform_peak(volatile uint32_t *peak,uint32_t value) {if(value>*peak)*peak=value;}
#endif
static inline void sw_platform_mask(volatile sw_platform_stats *s,uint32_t elapsed) {if(elapsed>s->mask_peak_ticks)s->mask_peak_ticks=elapsed;}
/* Lowest overwritten word is a conservative high-water estimate, not a
   proof of every executed stack path. Call only on painted stack memory. */
static inline uint32_t sw_platform_stack_used(const volatile uint32_t *low,const volatile uint32_t *high) {
 const volatile uint32_t *p=low;while(p<high&&*p==SW_STACK_PAINT)p++;
 return (uint32_t)(high-p)*4u;
}
#endif
