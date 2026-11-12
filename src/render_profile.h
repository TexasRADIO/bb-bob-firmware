/* SPDX-License-Identifier: GPL-3.0-only; optional wall-time probes, never a clock source for music. */
#ifndef SW_RENDER_PROFILE_H
#define SW_RENDER_PROFILE_H
#include <stdint.h>
typedef uint32_t (*sw_profile_clock)(void);
#if defined(SW_TARGET_RAM_AUDIO)&&!defined(__APPLE__)
#define SW_PROFILE_RAM __attribute__((section(".ram_audio")))
#else
#define SW_PROFILE_RAM
#endif
enum {
 SW_PROFILE_CONTROL,
 SW_PROFILE_CLASSIC,
 SW_PROFILE_VOICES,
 SW_PROFILE_STEEL,
 SW_PROFILE_FIDDLE,
 SW_PROFILE_GUITAR,
 SW_PROFILE_PIANO,
 SW_PROFILE_BASS,
 SW_PROFILE_DRUMS,
 SW_PROFILE_SYNTH,
 SW_PROFILE_PCM,
 SW_PROFILE_ACID,
 SW_PROFILE_RECORD,
 SW_PROFILE_ECHO,
 SW_PROFILE_CUE,
 SW_PROFILE_MIX,
 SW_PROFILE_NOTE,
 SW_PROFILE_SCOPES
};
typedef struct {
 sw_profile_clock clock;
 uint32_t enabled,force_synth,ticks[SW_PROFILE_SCOPES];
 uint32_t note_starts,frames,peak_voices;
} sw_render_profile;
static inline uint32_t sw_profile_now(const sw_render_profile *p) {
 return p->enabled&&p->clock?p->clock():0;
}
SW_PROFILE_RAM static inline uint32_t sw_profile_now_audio(const sw_render_profile *p) {
 return p->enabled&&p->clock?p->clock():0;
}
/* Unsigned deltas handle one timer wrap. Stage totals include preemption.
   Note-start time is nested inside control time; do not add both totals. */
static inline uint32_t sw_profile_split(sw_render_profile *p,uint32_t start,uint32_t *total) {
 uint32_t now=sw_profile_now(p);if(p->enabled&&p->clock)*total+=now-start;return now;
}
SW_PROFILE_RAM static inline uint32_t sw_profile_split_audio(sw_render_profile *p,uint32_t start,uint32_t *total) {
 uint32_t now=sw_profile_now_audio(p);if(p->enabled&&p->clock)*total+=now-start;return now;
}
static inline void sw_profile_reset(sw_render_profile *p) {
 for(uint32_t i=0;i<SW_PROFILE_SCOPES;i++)p->ticks[i]=0;
 p->note_starts=p->frames=p->peak_voices=0;
}
#endif
