/* SPDX-License-Identifier: GPL-3.0-only */
#include "sample_player.h"
#if defined(SW_TARGET_RAM_AUDIO)&&!defined(__APPLE__)
#define SW_RAM_AUDIO __attribute__((section(".ram_audio")))
#else
#define SW_RAM_AUDIO
#endif
static const int16_t steps[89]={7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,73,80,88,97,107,118,130,143,157,173,190,209,230,253,279,307,337,371,408,449,494,544,598,658,724,796,876,963,1060,1166,1282,1411,1552,1707,1878,2066,2272,2499,2749,3024,3327,3660,4026,4428,4871,5358,5894,6484,7132,7845,8630,9493,10442,11487,12635,13899,15289,16818,18500,20350,22385,24623,27086,29794,32767};
SW_RAM_AUDIO static int bound(int x,int a,int b){return x<a?a:x>b?b:x;}
void sw_sample_start(sw_sample_player *p,const sw_bank_clip *c,uint32_t selected) {
 p->retrigger_tail=p->active?p->last_output:0;
 p->clip=*c;p->position=p->decoded=p->output_frame=p->release=p->fraction=0;p->step=c->rate*4096u/44100u;p->fraction_step=c->rate*4096u%44100u;
 p->predictor=p->previous=c->initial;p->index=(int32_t)c->step_index;p->selected=(uint16_t)selected;p->active=1;
}
void sw_sample_stop(sw_sample_player *p){if(p->active&&!p->release)p->release=256;}
SW_RAM_AUDIO float sw_sample_read(sw_sample_player *p) {
 static const int8_t change[8]={-1,-1,-1,-1,2,4,6,8};
 if(!p->active)return 0;
 uint32_t at=p->position>>12;
 while(p->decoded<=at&&p->decoded+1u<p->clip.samples) {
  uint32_t n=p->decoded,code=(p->clip.pcm[n>>1]>>((n&1u)*4u))&15u;int step=steps[p->index],diff=(step>>3)+((code&4)?step:0)+((code&2)?step>>1:0)+((code&1)?step>>2:0);
  p->previous=p->predictor;p->predictor=bound(p->predictor+((code&8)?-diff:diff),-32768,32767);p->index=bound(p->index+change[code&7],0,88);p->decoded++;
 }
 float frac=(p->position&4095u)*(1.f/4096.f),value=p->decoded==at?(float)p->predictor:(float)p->previous+((float)p->predictor-(float)p->previous)*frac;
 float fade=p->output_frame<256?(float)p->output_frame/256.f:1.f;
 float end=(float)((p->clip.samples<<12)-p->position)/((float)p->step*256.f);if(end<fade)fade=end;
 if(p->release){fade*=(float)p->release/256.f;if(!--p->release)p->active=0;}
 p->position+=p->step;p->fraction+=p->fraction_step;if(p->fraction>=44100u){p->fraction-=44100u;p->position++;}p->output_frame++;if(p->position>=(p->clip.samples<<12))p->active=0;
 float output=value*(.45f/32768.f)*fade;
 if(p->output_frame<=256)output+=p->retrigger_tail*(1.f-(float)(p->output_frame-1)/256.f);
 p->last_output=output;return output;
}
