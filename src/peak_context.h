/* SPDX-License-Identifier: GPL-3.0-only; whole-half diagnostics, no sample-loop probes. */
#ifndef BB_PEAK_CONTEXT_H
#define BB_PEAK_CONTEXT_H
#include <stdint.h>
#define BB_PEAK_WORDS 19u
#define BB_PEAK_CONDITIONS 8u
#define BB_PEAK_PAGES 5u
/* Records describe render-exit musical state. Ticks include observation work up
 * to the last timer read; terminal counter/telemetry bookkeeping follows it. */
enum { BB_PC_VALID,BB_PC_HALF,BB_PC_TICKS,BB_PC_CLASSIC,BB_PC_SEED,BB_PC_BAR,
 BB_PC_TICK,BB_PC_SECTION,BB_PC_STYLE,BB_PC_VOICES,BB_PC_FAMILIES,BB_PC_NOTES,
 BB_PC_CALL,BB_PC_CALL_POSITION,BB_PC_FLAGS,BB_PC_RECORD,BB_PC_ACID_STARTS,
 BB_PC_BPM,BB_PC_FRAMES };
enum { BB_PC_DRAW=1u,BB_PC_DMA=2u,BB_PC_CALL_ON=4u,BB_PC_RECORDED=8u,BB_PC_FROZEN=16u };
typedef struct {uint32_t w[BB_PEAK_WORDS];} bb_peak_record;
typedef struct {uint32_t halves,over_budget,dma_misses,peak_ticks,total_low,total_high;} bb_peak_condition;
typedef struct {bb_peak_record first,highest;bb_peak_condition condition[BB_PEAK_CONDITIONS];uint32_t last_half;} bb_peak_state;
static inline void bb_peak_clear(bb_peak_state *s){
 for(uint32_t i=0;i<BB_PEAK_WORDS;i++)s->first.w[i]=s->highest.w[i]=0;
 for(uint32_t i=0;i<BB_PEAK_CONDITIONS;i++){bb_peak_condition *c=&s->condition[i];c->halves=c->over_budget=c->dma_misses=c->peak_ticks=c->total_low=c->total_high=0;}
 s->last_half=0;
}
static inline uint32_t bb_peak_mask(uint32_t flags,uint32_t notes){
 return 1u|((flags&BB_PC_DRAW)?2u:4u)|((flags&BB_PC_DMA)?8u:0u)|
  (notes?16u:0u)|((flags&BB_PC_CALL_ON)?32u:0u)|((flags&BB_PC_RECORDED)?64u:0u)|((flags&BB_PC_FROZEN)?128u:0u);
}
static inline void bb_peak_store(bb_peak_record *dest,const bb_peak_record *src){
 for(uint32_t i=0;i<BB_PEAK_WORDS;i++)dest->w[i]=src->w[i];
}
/* At most one accounted exposure per condition per half. Conditions overlap. */
static inline void bb_peak_observe(bb_peak_state *s,const bb_peak_record *r,uint32_t ticks,uint32_t missed,uint32_t budget,uint32_t mask){
 s->last_half=r->w[BB_PC_HALF];
 if(r->w[BB_PC_VALID]&&!s->first.w[BB_PC_VALID]&&ticks>=budget){bb_peak_store(&s->first,r);s->first.w[BB_PC_TICKS]=ticks;}
 if(r->w[BB_PC_VALID]&&(!s->highest.w[BB_PC_VALID]||ticks>s->highest.w[BB_PC_TICKS])){bb_peak_store(&s->highest,r);s->highest.w[BB_PC_TICKS]=ticks;}
 for(uint32_t i=0;i<BB_PEAK_CONDITIONS;i++)if(mask&(1u<<i)){
  bb_peak_condition *c=&s->condition[i];uint32_t old=c->total_low;
  c->halves++;c->over_budget+=ticks>=budget;c->dma_misses+=missed!=0;
  if(ticks>c->peak_ticks)c->peak_ticks=ticks;
  c->total_low+=ticks;c->total_high+=c->total_low<old;
 }
}
#endif
