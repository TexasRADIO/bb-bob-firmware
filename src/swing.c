/* SPDX-License-Identifier: GPL-3.0-only
 * Original bounded Western swing director and synthesizer, 2026.
 * No allocation, audio file, OS call or transcendentals in the renderer. */
#include "swing.h"
#ifndef SW_ACTIVE_VOICES
#define SW_ACTIVE_VOICES SW_VOICES
#endif
#if SW_ACTIVE_VOICES < 1 || SW_ACTIVE_VOICES > SW_VOICES
#error SW_ACTIVE_VOICES must fit the fixed voice pool
#endif
#include "tables.h"
#include "timbres.h"
#include "rhythm_timbres.h"
#include "reed_timbres.h"
#include "period_timbres.h"
#include "leon_clip.h"
#include "music_bank.h"
#include "classic_bank.h"
#ifdef SW_WITH_TIMBRE_BANK
#include "timbre_bank.h"
#endif
#ifdef SW_WITH_CUE
#include "leon_pcm.h"
#endif
#ifdef SW_TRACE_CLASSICS
extern void sw_trace_classic_note(uint32_t slot,uint32_t at,uint32_t duration,uint8_t pitch,int8_t cents);
#endif
#ifdef SW_TRACE_SCORES
extern void sw_trace_score_note(uint32_t slot,uint32_t at,uint32_t duration,uint8_t pitch,uint8_t flags);
#endif
#ifdef SW_TRACE_BAND
extern void sw_trace_band_note(const sw_engine *,uint8_t,uint8_t,uint8_t,float,uint32_t,float);
#endif
#define SW_SLIDE_FRAMES 3528u
static void clear(void *p, uint32_t n) { uint8_t *b=p; while(n--) *b++=0; }

/* The external fiddle/upright/piano bank already copies 79,200 bytes into
 * pool RAM at boot. These four smaller samples were still read from .text/XIP
 * inside the audio ISR. The FM-1 pool has enough independent headroom to copy
 * all 27,200 bytes once, avoiding a partial cache, eviction policy or musical
 * reduction. Native builds keep their original pointers unless the exact
 * target path is requested explicitly for parity testing. */
static const uint8_t (*period_archtop)[SW_ARCHTOP_LENGTH]=sw_archtop_pcm;
static const uint8_t (*period_kick)[SW_ACOUSTIC_KICK_LENGTH]=sw_acoustic_kick_pcm;
static const uint8_t (*period_snare)[SW_SNARE_LENGTH]=sw_snare_pcm;
static const uint8_t (*period_hat)[SW_ACOUSTIC_HAT_LENGTH]=sw_acoustic_hat_pcm;
#ifdef SW_TARGET_PERIOD_CACHE
#ifdef __APPLE__
#define SW_PERIOD_POOL __attribute__((section("__DATA,__period_cache"),aligned(4)))
#else
#define SW_PERIOD_POOL __attribute__((section(".pool"),aligned(4)))
#endif
static uint8_t period_archtop_cache[4][SW_ARCHTOP_LENGTH] SW_PERIOD_POOL;
static uint8_t period_kick_cache[2][SW_ACOUSTIC_KICK_LENGTH] SW_PERIOD_POOL;
static uint8_t period_snare_cache[2][SW_SNARE_LENGTH] SW_PERIOD_POOL;
static uint8_t period_hat_cache[2][SW_ACOUSTIC_HAT_LENGTH] SW_PERIOD_POOL;
#endif
#ifdef SW_TARGET_PERIOD_CACHE
static void sw_hot_tables_boot(void (*watchdog)(void));
#endif
void sw_period_timbres_boot(void (*watchdog)(void)) {
#ifdef SW_TARGET_PERIOD_CACHE
 const uint8_t *sources[]={&sw_archtop_pcm[0][0],&sw_acoustic_kick_pcm[0][0],&sw_snare_pcm[0][0],&sw_acoustic_hat_pcm[0][0]};
 uint8_t *targets[]={&period_archtop_cache[0][0],&period_kick_cache[0][0],&period_snare_cache[0][0],&period_hat_cache[0][0]};
 const uint32_t lengths[]={sizeof sw_archtop_pcm,sizeof sw_acoustic_kick_pcm,sizeof sw_snare_pcm,sizeof sw_acoustic_hat_pcm};
 uint32_t copied=0;
 for(uint32_t part=0;part<4;part++)for(uint32_t i=0;i<lengths[part];i++) {
  targets[part][i]=sources[part][i];
  if(watchdog&&!(++copied&1023u))watchdog();
 }
 sw_hot_tables_boot(watchdog);
 period_archtop=period_archtop_cache;period_kick=period_kick_cache;
 period_snare=period_snare_cache;period_hat=period_hat_cache;
#else
 (void)watchdog;
#endif
}
/* SW_TARGET_RAM_AUDIO (PI32 target only): the per-sample render path runs from RAM. It goes in
 * .ram_audio, which app.ld places in the RAMTEXT window and crt0 copies with .ram_text before boot
 * continues. This removes the loop's own XIP instruction fetches; remaining
 * XIP callees/data can still stall, and only target timing can show a benefit.
 * Host builds ignore it, and the rendered output is identical either way. */
#if defined(SW_TARGET_RAM_AUDIO)&&!defined(__APPLE__)
#define SW_RAM_AUDIO __attribute__((section(".ram_audio")))
#else
#define SW_RAM_AUDIO
#endif
/* Hot tables read for every live voice on every sample: copied once into pool RAM in the target
 * build, like the period samples. The values are identical, so the output is bit-exact. */
static const float *sine_table=sw_sine;
static const float *decode_table=sw_fiddle_decode;
static inline __attribute__((always_inline)) float osc(uint32_t p) { return sine_table[p>>22]; }
static inline __attribute__((always_inline)) float steel_harmonics(uint32_t phase) {
 float pluck=.48f*osc(phase)+.18f*osc(phase*2)+.09f*osc(phase*3)+.04f*osc(phase*5);
 return pluck+(.055f*osc(phase*4)+.035f*osc(phase*6)+.025f*osc(phase*8));
}
#ifdef SW_TARGET_PERIOD_CACHE
enum { SW_STEEL_CACHE_LOW=48, SW_STEEL_CACHE_HIGH=96, SW_STEEL_CACHE_SAMPLES=6400 };
static float sine_cache[1024] SW_PERIOD_POOL;
static float decode_cache[256] SW_PERIOD_POOL;
static float steel_cache[SW_STEEL_CACHE_SAMPLES] SW_PERIOD_POOL;
static uint16_t steel_offsets[SW_STEEL_CACHE_HIGH-SW_STEEL_CACHE_LOW+2] SW_PERIOD_POOL;
static unsigned steel_cache_ready;
static void sw_hot_tables_boot(void (*watchdog)(void)) {
 for(uint32_t i=0;i<1024u;i++)sine_cache[i]=sw_sine[i];
 for(uint32_t i=0;i<256u;i++)decode_cache[i]=sw_fiddle_decode[i];
 sine_table=sine_cache;decode_table=decode_cache;
 steel_cache_ready=0;
 unsigned used=0;
 for(unsigned pitch=SW_STEEL_CACHE_LOW;pitch<=SW_STEEL_CACHE_HIGH;pitch++) {
  uint32_t step=sw_pitch[pitch];
  /* Cache a suffix slightly longer than the normal unbent reach. Wide
     bends outside that suffix use the same live harmonic calculation. */
  unsigned len=(unsigned)(4294967296.f/(float)step*1.04f+4.f);
  if(len>SW_STRING_SAMPLES||len>SW_STEEL_CACHE_SAMPLES-used)return;
  steel_offsets[pitch-SW_STEEL_CACHE_LOW]=(uint16_t)used;
  uint32_t phase=step*(SW_STRING_SAMPLES-len);
  for(unsigned j=0;j<len;j++,phase+=step)steel_cache[used++]=steel_harmonics(phase);
  if(watchdog)watchdog();
 }
 steel_offsets[SW_STEEL_CACHE_HIGH-SW_STEEL_CACHE_LOW+1]=(uint16_t)used;
 steel_cache_ready=1;
}
#endif
static uint32_t random32(uint32_t *r) { uint32_t x=*r; x^=x<<13; x^=x>>17; x^=x<<5; return *r=x; }
/* Over GF(2), the 512th power of this xorshift is the XOR of
   the powers selected by this mask. See the basis-vector check in test_band. */
#if SW_STRING_SAMPLES != 512u
#error Update the bass noise jump when the string length changes
#endif
static uint32_t bass_noise_jump(uint32_t state) {
 uint32_t result=0;
 for(uint32_t powers=0x4de7b28fu;powers;powers>>=1) {
  if(powers&1u)result^=state;
  if(powers>1u)random32(&state);
 }
 return result;
}
static int clamp(int x,int lo,int hi) { return x<lo?lo:x>hi?hi:x; }
#if defined(SW_TARGET_RAM_AUDIO)&&!defined(__APPLE__)
SW_RAM_AUDIO static uint32_t random32_audio(uint32_t *r) { uint32_t x=*r; x^=x<<13; x^=x>>17; x^=x<<5; return *r=x; }
#endif
SW_RAM_AUDIO static int clamp_audio(int x,int lo,int hi) { return x<lo?lo:x>hi?hi:x; }
SW_RAM_AUDIO static float osc_audio(uint32_t p) { return sine_table[p>>22]; }
static float saturate(float x) {return x/(1.f+(x<0?-x:x));}
SW_RAM_AUDIO static float saturate_audio(float x) {return x/(1.f+(x<0?-x:x));}
/* Original band-limited saw. No third-party oscillator or circuit model. */
SW_RAM_AUDIO static float saw(uint32_t phase,uint32_t increment) {
 float t=(float)(phase>>8)*(1.f/16777216.f),dt=(float)increment*(1.f/4294967296.f),out=2.f*t-1.f;
 if(dt>0&&t<dt) {float x=t/dt;out-=x+x-x*x-1.f;}
 else if(dt>0&&t>1.f-dt) {float x=(t-1.f)/dt;out-=x*x+x+x+1.f;}
 return out;
}
static void record_cancel(sw_engine *e) {e->record_pending=0;if(e->record_active&&!e->record_release)e->record_release=256;}
uint32_t sw_record_available(void) {return SW_LEON_CLIP_FRAMES>0;}
static void record_start(sw_engine *e) {
 #if SW_LEON_CLIP_FRAMES > 0
 int delta=(int)e->song.key-(int)SW_LEON_CLIP_KEY;while(delta>6)delta-=12;while(delta<-6)delta+=12;
 e->record_pitch=(float)SW_LEON_CLIP_RATE/(float)SW_RATE*(float)sw_pitch[60+delta]/(float)sw_pitch[60];
 e->record_time_step=(float)SW_LEON_CLIP_FRAMES*(float)e->bpm/((float)SW_LEON_CLIP_BEATS*60.f*(float)SW_RATE);
 e->score_playing=0;e->record_position=0;e->record_grain[0]=e->record_grain[1]=0;e->record_clock=0;e->record_release=0;e->record_pending=0;e->record_active=1;e->record_until=e->playing?(uint16_t)(e->bar+2):0xffff;
 for(uint32_t i=0;i<SW_VOICES;i++)if(e->voice[i].player==SW_STEEL||e->voice[i].player==SW_FIDDLE) {e->voice[i].gate=0;e->voice[i].decay=.985f;}
 #else
 (void)e;
 #endif
}
#if SW_LEON_CLIP_FRAMES > 0 && defined(SW_LEON_CLIP_BLOCK_SHIFT)
static inline int32_t record_pcm_at(uint32_t index) {
 return sw_pcm_block_at(sw_leon_clip_bits,sw_leon_clip_blocks,index,SW_LEON_CLIP_BLOCK_SHIFT);
}
#endif
SW_RAM_AUDIO static float record_sample(sw_engine *e) {
 #if SW_LEON_CLIP_FRAMES > 0
 if(!e->record_active)return 0;
 uint32_t phase=e->record_clock&2047u;float result=0;
 for(uint32_t g=0;g<2;g++) {
  uint32_t p=(phase+g*1024u)&2047u;
  if(!p&&e->record_clock)e->record_grain[g]=e->record_position+1024.f*(e->record_time_step-e->record_pitch);
  float at=e->record_grain[g],sample=0;
  if(at>=0&&at<(float)(SW_LEON_CLIP_FRAMES-1)) {uint32_t n=(uint32_t)at;float frac=at-(float)n;
   #ifdef SW_LEON_CLIP_BLOCK_SHIFT
   int32_t a=record_pcm_at(n),b=record_pcm_at(n+1u);sample=(float)a+((float)b-(float)a)*frac;
   #else
   sample=(float)sw_leon_clip_pcm[n]+((float)sw_leon_clip_pcm[n+1]-(float)sw_leon_clip_pcm[n])*frac;
   #endif
  }
  float window=.5f-.5f*osc_audio((p<<21)+0x40000000u);result+=sample*window;e->record_grain[g]+=e->record_pitch;
 }
 float fade=((float)SW_LEON_CLIP_FRAMES-e->record_position)/(e->record_time_step*256.f);if(fade>1)fade=1;if(fade<0)fade=0;
 if(e->record_release) {fade*=(float)e->record_release/256.f;if(!--e->record_release)e->record_active=0;}
 e->record_position+=e->record_time_step;e->record_clock++;if(e->record_position>=(float)SW_LEON_CLIP_FRAMES)e->record_active=0;
 return result*(.62f/128.f)*fade;
 #else
 (void)e;return 0;
 #endif
}
/* Ninth uses a root/third/seventh/ninth shell; the bass retains the fifth. */
static const uint8_t intervals[9][4]={{0,4,7,9},{0,4,7,10},{0,3,7,10},{0,3,6,9},{0,4,8,10},{0,3,7,9},{0,4,7,11},{0,4,10,14},{0,4,8,12}};
static const uint8_t aroot[3][8]={{0,0,5,2,0,9,2,7},{0,9,2,7,0,5,2,7},{0,0,5,6,0,9,2,7}};
static const uint8_t aqu[3][8]={{0,0,0,1,0,1,2,1},{0,1,2,1,0,0,2,1},{0,1,0,3,0,1,2,1}};
static const uint8_t bluesroot[12]={0,5,0,0,5,5,0,9,2,7,0,7};
static const int8_t contours[4][8]={{0,2,4,2,0,-2,1,0},{0,1,3,1,-1,0,2,0},{0,-2,-3,0,2,4,1,0},{0,3,1,-1,0,2,-2,0}};
static const uint8_t rhythms[4]={0x5b,0x6d,0xb5,0x57};
static uint8_t closest(sw_chord c,int desired,int lo,int hi) {
 int n,j,best=lo,distance=200;
 for(n=lo;n<=hi;n++) for(j=0;j<4;j++) if(n%12==(c.root+intervals[c.quality][j])%12) {
  int d=n>desired?n-desired:desired-n;
  if(d<distance) { distance=d; best=n; }
 }
 return (uint8_t)best;
}
void sw_generate(sw_song *song,uint32_t seed,uint8_t key,uint8_t form) {
 uint32_t r=seed^0x5a17c93bu,variant,motif,anchor,b,t; int previous;
 if(!r)r=1;
 clear(song,sizeof *song);song->seed=seed;song->key=key%12;song->form=form%3;song->beats=4;
 song->count=song->form==2?48:song->form?44:56;variant=random32(&r)%3;motif=random32(&r)%SW_CELL_COUNT;anchor=65+key%7;
 previous=anchor;
 for(b=0;b<song->count;b++) {
  sw_bar *bar=&song->bars[b]; uint32_t degree=0,quality=0,section=SW_THEME,phase=0;
  if(b<4) { const uint8_t in[4]={0,9,2,7};degree=in[b];quality=b==0?0:b==2?2:1;section=SW_INTRO; }
  else if(b>=song->count-4) { const uint8_t end[4]={5,2,7,0};degree=end[b-(song->count-4)];quality=degree==0||degree==5?0:degree==2?2:1;section=SW_ENDING; }
  else if(!song->form) {
   phase=(b-4)%8;
   if(b>=20&&b<28) { const uint8_t br[8]={4,4,9,9,2,2,7,7};degree=br[phase];quality=1;section=SW_BRIDGE; }
   else { degree=aroot[variant][phase];quality=aqu[variant][phase]; }
   if(b>=36&&b<44)section=SW_SOLO;
   if(b>=44&&b<52)section=SW_RETURN;
   if((b==35||b==51)) { degree=0;quality=0; }
  } else if(song->form==2) {
   phase=(b-4)%8;degree=aroot[variant][phase];quality=aqu[variant][phase];
   if(b>=12&&b<20) {section=SW_BRIDGE;degree=(phase<4?5:7);quality=phase<4?0:1;}
   if(b>=20&&b<36)section=SW_SOLO;
   if(b>=36)section=SW_RETURN;
  } else {
   phase=(b-4)%12;degree=bluesroot[phase];quality=phase==8?2:1;
   if(b>=16&&b<28)section=SW_SOLO;
   if(b>=28)section=SW_RETURN;
   if(b==39) { degree=0;quality=0; }
  }
  bar->section=(uint8_t)section;bar->chord[0].root=(song->key+degree)%12;bar->chord[0].quality=(uint8_t)quality;
  bar->chord[1]=bar->chord[0];
  if(!song->form&&phase==6&&b>=4&&b<song->count-4) {bar->chord[1].root=(song->key+7)%12;bar->chord[1].quality=1;}
  bar->mask=rhythms[(motif+((phase/2)&1))%4];
  if(b<4)bar->mask= b==3?0x50:0;
  if(b==song->count-1)bar->mask=1;
  /* Two-bar thematic contours recur; structural positions follow the active chord. */
  if(!(phase&1))previous=anchor;
  for(t=0;t<8;t++) {
   sw_chord c=bar->chord[t/4];int desired=previous+contours[motif%4][t];
   if(section==SW_SOLO)desired+=((int)(random32(&r)%5))-2;
   bar->melody[t]=closest(c,desired,62,81);previous=bar->melody[t];
  }
  if(b==song->count-1)bar->melody[0]=(uint8_t)(72+song->key);
  /* Corrected rhythm cells drive the original tune, with harmonic anchors and
     newly composed answers. Return sections recover the same seed/cell. */
  uint32_t cell=(motif+phase/2)%SW_CELL_COUNT;bar->phrase_id=(uint8_t)cell;
  const sw_cell *cellp=&sw_cells[cell];int start=anchor+((phase&1)?2:0);
  for(uint32_t n=0;n<cellp->count;n++) {
   uint32_t tick=cellp->tick[n];int desired=start+cellp->pitch[n];
   if((phase&1)&&n>0)desired=(int)bar->lead[cellp->tick[n-1]]+sw_answers[(motif+phase/2)%3][(n-1)%5];
   if(section==SW_SOLO)desired+=((int)(random32(&r)%3))-1;
   sw_chord chord=bar->chord[tick/8];
   /* Arrivals are chord tones; interior chromatic approaches retain contour. */
   int pitch=(n==0||n+1==cellp->count||!(tick%4))?closest(chord,desired,62,84):clamp(desired,62,84);
   bar->lead[tick]=(uint8_t)pitch;bar->gate[tick]=cellp->gate[n];bar->lead_mask|=(uint16_t)(1u<<tick);
  }
  if(section==SW_INTRO) {bar->lead_mask=b==3?0x9000:0;bar->lead[12]=closest(bar->chord[1],anchor-2,62,84);bar->lead[15]=closest(bar->chord[1],anchor,62,84);bar->gate[12]=bar->gate[15]=1;}
  if(b==song->count-1) {bar->lead_mask=1;bar->lead[0]=(uint8_t)(72+song->key);bar->gate[0]=12;}

 }
}
const char *sw_section_name(uint32_t s) { static const char *const names[]={"INTRO","THEME","BRIDGE","STEEL BREAK","THEME RETURNS","LAST ROUND"};return names[s%6]; }
const char *sw_player_name(uint32_t p) { static const char *const names[]={"STEEL","FIDDLE","GUITAR","PIANO","BASS","DRUMS","REEDS","SYNTH"};return names[p%8]; }
const char *sw_player_label(const sw_engine *e,uint32_t p) {
 static const char *const wired[]={"CHROME","STRNGS","E-GTR","KEYS","ACID","DANCE","BRASS","SYNTH"};
 return e->modern_target>=.35f?wired[p%8]:sw_player_name(p);
}
#ifdef SW_WITH_BANK
#include "bank.h"
static sw_bank call_bank;
int sw_calls_bank(const uint8_t *bytes,uint32_t length,uint32_t limit) {
 if(!sw_bank_open(&call_bank,bytes,length,limit))return 0;
 if(!call_bank.calls||call_bank.calls>SW_CALL_CAPACITY) {call_bank.bytes=0;return 0;}
 return 1;
}
#endif
#ifdef SW_WITH_CUE
#if SW_CALL_COUNT > SW_CALL_CAPACITY
#error Embedded call count exceeds index capacity
#endif
/* R131: the device uses embedded calls, not the external bank. Cache its
 * hot directory and tiny decode tables before IRQs start. Compressed clip bytes,
 * sample rates, interpolation and gain are unchanged. Default pointers preserve
 * source-only/no-boot host behaviour. No cache fill occurs in the audio ISR. */
static const int16_t call_steps[89]={7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,73,80,88,97,107,118,130,143,157,173,190,209,230,253,279,307,337,371,408,449,494,544,598,658,724,796,876,963,1060,1166,1282,1411,1552,1707,1878,2066,2272,2499,2749,3024,3327,3660,4026,4428,4871,5358,5894,6484,7132,7845,8630,9493,10442,11487,12635,13899,15289,16818,18500,20350,22385,24623,27086,29794,32767};
static const int8_t call_change[8]={-1,-1,-1,-1,2,4,6,8};
static const uint8_t *const *call_data_table=sw_call_pcm;
static const uint32_t *call_frame_table=sw_call_samples;
static const int16_t *call_initial_table=sw_call_initial;
static const int16_t *call_step_table=call_steps;
static const int8_t *call_change_table=call_change;
#ifdef SW_CALL_HUFFMAN_BITS
static const uint8_t *call_prefix_table=sw_call_huffman;
#endif
#ifdef SW_TARGET_CALL_CACHE
#ifdef __APPLE__
#define SW_CALL_POOL __attribute__((section("__DATA,__call_cache"),aligned(4)))
#else
#define SW_CALL_POOL __attribute__((section(".pool"),aligned(4)))
#endif
static const uint8_t *call_data_cache[SW_CALL_COUNT] SW_CALL_POOL;
static uint32_t call_frame_cache[SW_CALL_COUNT] SW_CALL_POOL;
static int16_t call_initial_cache[SW_CALL_COUNT] SW_CALL_POOL;
static int16_t call_step_cache[89] SW_CALL_POOL;
static int8_t call_change_cache[8] SW_CALL_POOL;
#ifdef SW_CALL_HUFFMAN_BITS
static uint8_t call_prefix_cache[sizeof sw_call_huffman] SW_CALL_POOL;
#endif
#endif
static uint32_t call_flags(uint32_t index) {
 #ifdef SW_WITH_BANK
 if(call_bank.bytes) {sw_bank_clip c;return sw_bank_call(&call_bank,index,&c)?c.flags:0;}
 #endif
 return sw_call_flags[index];
}
static uint32_t call_frames(uint32_t index) {
 #ifdef SW_WITH_BANK
 if(call_bank.bytes) {sw_bank_clip c;return sw_bank_call(&call_bank,index,&c)?c.samples:0;}
 #endif
 return call_frame_table[index];
}
#ifdef SW_WITH_BANK
/* Audio context only: resolve one bank entry per call rather than walking the
 * flash-resident directory for every output sample. The main loop retains its
 * uncached lookup, so the two contexts never share mutable cache state. */
static sw_bank_clip audio_clip;static const uint8_t *audio_clip_bank;static uint32_t audio_clip_kind=0xffffffffu;
SW_RAM_AUDIO static const sw_bank_clip *audio_call_clip(uint32_t index) {
 if(audio_clip_kind!=index||audio_clip_bank!=call_bank.bytes) {
  if(!sw_bank_call(&call_bank,index,&audio_clip))clear(&audio_clip,sizeof audio_clip);
  audio_clip_kind=index;audio_clip_bank=call_bank.bytes;
 }
 return &audio_clip;
}
#endif
SW_RAM_AUDIO static uint32_t call_frames_audio(uint32_t index) {
 #ifdef SW_WITH_BANK
 if(call_bank.bytes)return audio_call_clip(index)->samples;
 #endif
 return call_frame_table[index];
}
#endif
/* Independent shuffle bags use only reviewed pool membership. */
static void call_pick(sw_engine *e,uint32_t pool) {
 #ifdef SW_WITH_CUE
 if(pool>=SW_CALL_POOLS)return;
 uint32_t n=e->call_remaining[pool];
 if(!n) {
  for(uint32_t i=0;i<sw_call_count();i++)if(call_flags(i)&(1u<<pool))e->call_bag[pool][n++]=(sw_call_index)i;
  if(!n)return;
  for(uint32_t i=n-1;i>0;i--) {uint32_t j=random32(&e->call_random)%(i+1);sw_call_index v=e->call_bag[pool][i];e->call_bag[pool][i]=e->call_bag[pool][j];e->call_bag[pool][j]=v;}
  if(n>1&&e->call_bag[pool][n-1]==e->call_last[pool]) {sw_call_index v=e->call_bag[pool][n-1];e->call_bag[pool][n-1]=e->call_bag[pool][0];e->call_bag[pool][0]=v;}
 }
 e->cue_kind=e->call_bag[pool][--n];e->call_last[pool]=e->cue_kind;e->call_remaining[pool]=(sw_call_index)n;e->cue_position=0;e->title=0;
 #else
 (void)e;(void)pool;
 #endif
}
#ifdef SW_WITH_CUE
SW_RAM_AUDIO static float call_sample(sw_engine *e) {
 #if defined(SW_CALL_HUFFMAN_BITS) && (SW_CALL_HUFFMAN_BITS < 1 || SW_CALL_HUFFMAN_BITS > 8)
 #error Unsupported call prefix table size
 #endif
 uint32_t at=e->cue_position>>12,frames,step,initial_index=0;int32_t initial;
 const uint8_t *data;
 #ifdef SW_WITH_BANK
 uint32_t external=call_bank.bytes!=0;
 if(external) {const sw_bank_clip c=*audio_call_clip(e->cue_kind);data=c.pcm;frames=c.samples;initial=c.initial;initial_index=c.step_index;step=(c.rate*4096u+SW_RATE/2u)/SW_RATE;}
 else
 #endif
 {data=call_data_table[e->cue_kind];frames=call_frame_table[e->cue_kind];initial=call_initial_table[e->cue_kind];step=SW_CALL_STEP;}
 if(!e->cue_position) {e->call_decoded=e->call_bit_position=0;e->call_predictor=e->call_previous=initial;e->call_step_index=(int32_t)initial_index;}
 while(e->call_decoded<=at&&e->call_decoded+1<frames) {
  uint32_t code;
  #ifdef SW_WITH_BANK
  if(external) {uint32_t n=e->call_decoded;code=(data[n>>1]>>((n&1u)*4u))&15u;}
  else
  #endif
  {
  #ifdef SW_CALL_HUFFMAN_BITS
  /* At most8 prefix bits, one two-byte peek, one lookup. A zero guard byte
     makes the final partial prefix readable without another call buffer. */
  uint32_t byte=e->call_bit_position>>3,shift=e->call_bit_position&7u;
  uint32_t pair=((uint32_t)data[byte]<<8)|data[byte+1u];
  uint32_t entry=call_prefix_table[(pair>>(16u-SW_CALL_HUFFMAN_BITS-shift))&((1u<<SW_CALL_HUFFMAN_BITS)-1u)];
  code=entry&15u;e->call_bit_position+=entry>>4;
  #else
  uint32_t n=e->call_decoded;code=(data[n>>1]>>((n&1u)*4u))&15u;
  #endif
  }
  int step=call_step_table[e->call_step_index],diff=(step>>3)+((code&4)?step:0)+((code&2)?step>>1:0)+((code&1)?step>>2:0);
  e->call_previous=e->call_predictor;e->call_predictor=clamp_audio(e->call_predictor+((code&8)?-diff:diff),-32768,32767);
  e->call_step_index=clamp_audio(e->call_step_index+call_change_table[code&7],0,88);e->call_decoded++;
 }
 float fraction=(e->cue_position&4095u)*(1.f/4096.f);
 float value=(e->call_decoded==at)?(float)e->call_predictor:(float)e->call_previous+((float)e->call_predictor-(float)e->call_previous)*fraction;
 e->cue_position+=step;return value*(.24f/32768.f);
}
#endif
void sw_call_tables_boot(void (*watchdog)(void)) {
#if defined(SW_WITH_CUE) && defined(SW_TARGET_CALL_CACHE)
 for(uint32_t i=0;i<SW_CALL_COUNT;i++) {
  call_data_cache[i]=sw_call_pcm[i];call_frame_cache[i]=sw_call_samples[i];call_initial_cache[i]=sw_call_initial[i];
  if(watchdog&&(i&31u)==31u)watchdog();
 }
 for(uint32_t i=0;i<89u;i++)call_step_cache[i]=call_steps[i];
 for(uint32_t i=0;i<8u;i++)call_change_cache[i]=call_change[i];
 #ifdef SW_CALL_HUFFMAN_BITS
 for(uint32_t i=0;i<sizeof sw_call_huffman;i++)call_prefix_cache[i]=sw_call_huffman[i];
 call_prefix_table=call_prefix_cache;
 #endif
 call_data_table=call_data_cache;call_frame_table=call_frame_cache;call_initial_table=call_initial_cache;
 call_step_table=call_step_cache;call_change_table=call_change_cache;
 if(watchdog)watchdog();
#else
 (void)watchdog;
#endif
}
uint32_t sw_call_count(void) {
 #ifdef SW_WITH_CUE
 #ifdef SW_WITH_BANK
 if(call_bank.bytes)return call_bank.calls;
 #endif
 return SW_CALL_COUNT;
 #else
 return 0;
 #endif
}
uint32_t sw_calls_historic(void) {
 #ifdef SW_WITH_CUE
 #ifdef SW_WITH_BANK
 if(call_bank.bytes)return 1;
 #endif
 return SW_CALL_HISTORIC;
 #else
 return 0;
 #endif
}
uint32_t sw_call_active(const sw_engine *e) {
 #ifdef SW_WITH_CUE
 return e->cue_kind<sw_call_count()&&e->cue_position<(call_frames(e->cue_kind)<<12);
 #else
 (void)e;return 0;
 #endif
}
uint32_t sw_call_targets(const sw_engine *e) {
 #ifdef SW_WITH_BANK
 if(call_bank.bytes) {sw_bank_clip c;return sw_bank_call(&call_bank,e->cue_kind,&c)?c.targets:0;}
 #endif
 (void)e;return 0;
}
uint32_t sw_sample_count(void) {
 #ifdef SW_WITH_BANK
 uint32_t n=0;sw_bank_clip clip;
 for(uint32_t i=call_bank.calls;i<call_bank.count;i++)if(sw_bank_audio(&call_bank,i,&clip))n++;
 return n;
 #else
 return 0;
 #endif
}
#ifdef SW_WITH_BANK
static int sample_clip(uint32_t index,sw_bank_clip *out) {
 for(uint32_t i=call_bank.calls,n=0;i<call_bank.count;i++)if(sw_bank_audio(&call_bank,i,out)&&n++==index)return 1;
 return 0;
}
#endif
uint32_t sw_sample_active(const sw_engine *e) {
 #ifdef SW_WITH_BANK
 return e->sample_player.active;
 #else
 (void)e;return 0;
 #endif
}
uint32_t sw_sample_selected(const sw_engine *e) {
 #ifdef SW_WITH_BANK
 return e->sample_selected;
 #else
 (void)e;return 0;
 #endif
}
uint32_t sw_sample_playing(const sw_engine *e) {
 #ifdef SW_WITH_BANK
 return e->sample_player.selected;
 #else
 (void)e;return 0;
 #endif
}
uint32_t sw_sample_position(const sw_engine *e) {
 #ifdef SW_WITH_BANK
 return e->sample_player.position>>12;
 #else
 (void)e;return 0;
 #endif
}
uint32_t sw_sample_targets(const sw_engine *e) {
 #ifdef SW_WITH_BANK
 return e->sample_player.active?e->sample_player.clip.targets:0;
 #else
 (void)e;return 0;
 #endif
}
uint32_t sw_bob_frame(const sw_engine *e) {
 uint32_t tap=e->playing&&e->tick&&((e->tick-1)&1u)==0;
 return tap+((sw_call_active(e)&&e->call_envelope>.055f)?2u:0u);
}
void sw_visual_advance(sw_engine *e,uint32_t milliseconds) {
 if(milliseconds>1000)milliseconds=1000;
 e->visual_ms+=milliseconds;
 if(sw_call_active(e)||e->title)return;
 if(e->gesture_active) {
  e->gesture_time+=milliseconds;
  if(e->gesture_time>=2000) {e->gesture_active=0;e->gesture_wait=4000+random32(&e->visual_random)%6001;}
 } else if(milliseconds>=e->gesture_wait) {
  uint8_t next=(uint8_t)(random32(&e->visual_random)%6);
  if(next==e->gesture)next=(uint8_t)((next+1)%6);
  e->gesture=next;e->gesture_time=0;e->gesture_active=1;
 } else e->gesture_wait-=milliseconds;
}
const char *sw_call_label(const sw_engine *e) {
 #ifdef SW_WITH_CUE
 #ifdef SW_WITH_BANK
 if(call_bank.bytes) {sw_bank_clip c;return sw_bank_call(&call_bank,e->cue_kind%call_bank.calls,&c)?c.label:"CALL";}
 #endif
 return sw_call_labels[e->cue_kind%SW_CALL_COUNT];
 #else
 (void)e;return "No call bank";
 #endif
}
const char *sw_call_short_label(uint32_t index) {
 #ifdef SW_WITH_CUE
 #ifdef SW_WITH_BANK
 if(call_bank.bytes) {sw_bank_clip c;return sw_bank_call(&call_bank,index%call_bank.calls,&c)?c.short_label:"CALL";}
 #endif
 return sw_call_pad_labels[index%SW_CALL_COUNT];
 #else
 (void)index;return "CALL";
 #endif
}
void sw_init(sw_engine *e,uint32_t seed) {
 clear(e,sizeof *e);sw_generate(&e->song,seed,2,0);e->noise=0x329183u;e->bpm=168;e->swing=59;
 e->classic_cycle=2;e->players=0x3f;e->score_scheduled=0xffff;e->source_solos=1;e->texture=70;e->master=e->master_target=.65f;e->air=.24f;e->glow=.5f;e->swell=.28f;e->key_next=2;
 e->solo_from=0xffff;e->solo_until=0xffff;e->song_revision=1;e->cue_position=0xffffffffu;e->spoken=1;e->call_random=seed^0xb0bca11u;for(uint32_t i=0;i<SW_CALL_POOLS;i++)e->call_last[i]=(sw_call_index)~(sw_call_index)0;e->title=1;e->auto_calls=1;
 if(!e->call_random)e->call_random=1;
 e->visual_random=seed^0x6b0bba11u;if(!e->visual_random)e->visual_random=1;e->gesture_wait=4500;
}
static int prepare_original(sw_engine *e,uint32_t seed,uint16_t bpm,uint16_t swing) {
 if(e->pending_ready)return 0;
 sw_generate(&e->pending,seed,e->key_next,e->form_next);
 e->pending_bpm=bpm;e->pending_swing=swing;
 __asm__ __volatile__("" ::: "memory");
 e->pending_ready=1;return 1;
}
int sw_prepare(sw_engine *e,uint32_t seed) {return prepare_original(e,seed,0,0);}
uint32_t sw_classic_slots(void) {return SW_CLASSIC_SLOTS;}
uint32_t sw_classic_available(uint32_t index) {return index<SW_CLASSIC_SLOTS&&classics[index].bytes>0;}
/* Run lengths are half-quarter cells, not musical beats. A bounded scan keeps
   all original chord starts without an expanded RAM copy or mutable cursor. */
static uint8_t classic_chord_at(const sw_classic *c,uint32_t index) {
 const uint8_t *run=c->chords;
 if(!run||index>=c->body/48u)return 0;
 for(uint32_t n=0;n<c->body/48u&&run[0];n++,run+=2) {
  if(index<run[0])return run[1];
  index-=run[0];
 }
 return 0;
}
/* Only explicitly mixed-meter source settings opt in. Both 6/8 and 3/4
 * occupy three transport quarters; their accompaniment accents differ.
 * Intro/ending are waltz, and features/returns map back to source bars. */
static uint32_t classic_grouping(const sw_engine *e) {
 if(!e->song.classic_id)return 0;
 const sw_classic *c=&classics[e->song.classic_id-1];
 if(!c->compound_bars)return 0;
 if(e->bar<4u||e->bar>=e->song.count-4u)return 2;
 uint32_t phase=e->bar-4u;
 if(phase>=c->bars+c->solo)phase=phase-c->bars-c->solo+c->ret_start;
 else if(phase>=c->bars)phase=phase-c->bars+c->solo_start;
 return phase>=c->compound_start&&phase<c->compound_start+c->compound_bars?1u:2u;
}
/* One word consumes one dictionary byte or one3-byte literal. No expansion
   buffer, allocation or variable bit loop; failed reads leave the cursor. */
static int classic_read_word(const sw_classic *c,uint32_t *offset,uint16_t *word) {
 uint32_t at=*offset;if(at>=c->bytes)return 0;
 uint32_t code=c->notes[at++];
 if(code==255u) {
  if(c->bytes-at<2u)return 0;
  *word=(uint16_t)(c->notes[at]|(uint16_t)c->notes[at+1]<<8);at+=2;
 } else {
  if(code>=SW_CLASSIC_DICTIONARY_COUNT)return 0;
  *word=classic_dictionary[code];
 }
 *offset=at;return 1;
}
#ifdef SW_TRACE_CLASSICS
uint8_t sw_classic_chord_probe(uint32_t slot,uint32_t index) {return slot<SW_CLASSIC_SLOTS?classic_chord_at(&classics[slot],index):0;}
int sw_classic_word_probe(uint32_t slot,uint32_t offset,uint16_t *word,uint32_t *next) {
 *next=offset;return slot<SW_CLASSIC_SLOTS?classic_read_word(&classics[slot],next,word):0;
}
int sw_classic_decode_probe(const uint8_t *data,uint32_t size,uint32_t offset,uint16_t *word,uint32_t *next) {
 sw_classic c={0};c.notes=data;c.bytes=size;*next=offset;return classic_read_word(&c,next,word);
}
#endif
uint32_t sw_classic_ready_count(void) {uint32_t count=0;for(uint32_t i=0;i<SW_CLASSIC_SLOTS;i++)count+=sw_classic_available(i);return count;}
const char *sw_classic_title(uint32_t index) {return index<SW_CLASSIC_SLOTS?classic_titles[index]:"No classic";}
uint32_t sw_classic_preview(uint32_t index,uint8_t *key,uint16_t *tempo) {
 if(!sw_classic_available(index))return 0;
 if(key)*key=classics[index].key;if(tempo)*tempo=classics[index].tempo;return 1;
}
uint32_t sw_call_metadata_flags(uint32_t index) {
 #ifdef SW_WITH_CUE
 return index<sw_call_count()?call_flags(index):0;
 #else
 (void)index;return 0;
 #endif
}
const char *sw_section_label(const sw_engine *e) {
 uint32_t b=e->bar<e->song.count?e->bar:e->song.count-1;
 if(e->score_playing&&e->score_arrangement)return sw_score_short_label(e->score_selected);
 if(e->solo_from!=0xffff&&b>=e->solo_from&&b<e->solo_until)return "LEON FEATURE";
 if(e->song.classic_id&&e->solo_from==0xffff) {
  const sw_classic *c=&classics[e->song.classic_id-1];
  if(b>=4u+c->free_start&&b<4u+c->free_start+c->free_bars)return c->lead==SW_GUITAR?"GUITAR AD-LIB":"AD-LIB";
 }
 return sw_section_name(e->song.bars[b].section);
}
int sw_prepare_classic(sw_engine *e,uint32_t index) {
 return sw_prepare_classic_seed(e,index,e->song.seed);
}
static int prepare_classic_timing(sw_engine *e,uint32_t index,uint32_t seed,uint16_t bpm,uint16_t swing) {
 if(e->pending_ready||!sw_classic_available(index))return 0;
 const sw_classic *classic=&classics[index];sw_song *song=&e->pending;
 sw_generate(song,seed,e->key_next,0);song->classic_id=(uint8_t)(index+1);song->beats=classic->beats;
 song->count=(uint8_t)(8+classic->bars+classic->solo+classic->ret);
 for(uint32_t b=0;b<song->count;b++) {
  uint32_t phase=b<4?0:(b-4)%classic->bars;sw_bar bar=song->bars[4+phase%8];
  bar.section=b<4?SW_INTRO:b>=song->count-4?SW_ENDING:b<4u+classic->bars?SW_THEME:b<4u+classic->bars+classic->solo?SW_SOLO:SW_RETURN;
  if(bar.section==SW_RETURN)phase=b-4-classic->bars-classic->solo+classic->ret_start;
  if(bar.section==SW_SOLO)phase=(b-4-classic->bars+classic->solo_start)%classic->bars;
  /* Printed slash choruses sit inside the source form. Keep their source
     harmony position; they are not an appended solo after the whole head. */
  if(b>=4u+classic->free_start&&b<4u+classic->free_start+classic->free_bars)bar.section=SW_SOLO;
  for(uint32_t h=0;h<2;h++) {
   uint8_t packed=classic_chord_at(classic,(phase*classic->beats*2+h*classic->beats)%(classic->body/48));
   int delta=(int)song->key-classic->key;bar.chord[h].root=(uint8_t)((packed%16+delta+12)%12);bar.chord[h].quality=packed>>4;
   if(bar.section==SW_INTRO||bar.section==SW_ENDING) {bar.chord[h].root=(uint8_t)((song->key+((b==3||b==song->count-2)?7:0))%12);bar.chord[h].quality=(b==3||b==song->count-2)?1:0;}
  }
  for(uint32_t t=0;t<16;t++)if(bar.lead_mask&(1u<<t))bar.lead[t]=closest(bar.chord[t/8],bar.lead[t],62,84);
  if(bar.section==SW_INTRO)bar.lead_mask=0;
  if(b==song->count-1) {bar.lead_mask=1;bar.lead[0]=(uint8_t)(72+song->key);bar.gate[0]=12;}
  song->bars[b]=bar;
 }
 e->pending_bpm=bpm?bpm:classic->tempo;e->pending_swing=swing;
 __asm__ __volatile__("" ::: "memory");e->pending_ready=1;return 1;
}
int sw_prepare_classic_seed(sw_engine *e,uint32_t index,uint32_t seed) {return prepare_classic_timing(e,index,seed,0,0);}
int sw_prepare_recall(sw_engine *e,uint32_t seed,uint32_t classic_id,uint16_t bpm,uint16_t swing) {
 if(bpm<80||bpm>220||swing<50||swing>68)return 0;
 return classic_id?prepare_classic_timing(e,classic_id-1,seed,bpm,swing):prepare_original(e,seed,bpm,swing);
}
static void adopt_pending(sw_engine *e) {
 e->song=e->pending;
 if(e->pending_bpm)e->bpm=e->pending_bpm;
 if(e->pending_swing)e->swing=e->pending_swing;
 e->clock_remainder=0;e->guitar_voiced=0;e->pending_ready=0;
}
static void hush(sw_engine *e) {e->score_playing=0;e->score_scheduled=0xffff;e->lead_second=0;e->classic_cycle=2; e->acid.gate=0;e->acid.sliding=0;e->acid.prev_slide=0;e->acid.sweep=0;uint32_t i;for(i=0;i<SW_VOICES;i++) if(e->voice[i].active) {e->voice[i].gate=0;e->voice[i].decay=.985f;} }
static sw_voice *note_ranked(sw_engine *e,uint8_t player,uint8_t type,uint8_t pitch,float amp,uint32_t gate,float pan,int bend,uint8_t foreground);
static sw_voice *note(sw_engine *e,uint8_t player,uint8_t type,uint8_t pitch,float amp,uint32_t gate,float pan,int bend) {
 return note_ranked(e,player,type,pitch,amp,gate,pan,bend,0);
}
static sw_voice *foreground_note(sw_engine *e,uint8_t player,uint8_t type,uint8_t pitch,float amp,uint32_t gate,float pan,int bend) {
 return note_ranked(e,player,type,pitch,amp,gate,pan,bend,1);
}
static void acid_note(sw_engine *e,int pitch,uint32_t gate,int slide,int accent);
#include "acid_phrases.h"
static uint32_t classic_warp(uint32_t position,uint32_t swing,uint32_t inverse);
static uint32_t band_position(const sw_engine *e) {
 uint32_t position=e->bar*e->song.beats*96u*256u;
 if(e->tick&&e->tick_length)position+=(e->tick-1)*48u*256u+(e->tick_length-e->tick_remaining)*(48u*256u)/e->tick_length;
 return position;
}
static uint32_t tulsa_bars(const sw_engine *e) {uint32_t ticks=e->song.beats*960u;return SW_TULSA_BODY_TICKS%ticks?0:SW_TULSA_BODY_TICKS/ticks;}
static uint32_t tulsa_fits(const sw_engine *e,uint32_t from) {uint32_t bars=tulsa_bars(e);return bars&&from+bars<=e->song.count-4u;}
static sw_chord tulsa_chord(const sw_engine *e,uint32_t at) {
 sw_chord c={7,0};for(uint32_t i=0;i<sizeof sw_tulsa_chords/sizeof sw_tulsa_chords[0];i++)if(at>=sw_tulsa_chords[i].at) {c.root=sw_tulsa_chords[i].root;c.quality=sw_tulsa_chords[i].quality;}
 c.root=(uint8_t)((c.root+(int)e->song.key-7+12)%12);return c;
}
static sw_chord tulsa_next_chord(const sw_engine *e) {
 uint32_t body=e->score_tick>SW_TULSA_PICKUP_TICKS?e->score_tick-SW_TULSA_PICKUP_TICKS:0;
 return tulsa_chord(e,SW_TULSA_PICKUP_TICKS+(body/(e->song.beats*960u)+1)*e->song.beats*960u);
}
const char *sw_score_short_label(uint32_t index) {static const char *const names[]={"LEON TULSA16","LEON SPANISH4","BOGGS COMP1","FIDDLE REF1","DUNN LEAD1","MURPHEY LEAD1"};return index<6?names[index]:"SOURCE STUDY";}
uint32_t sw_score_count(void) {return SW_SCORE_COUNT;}
static void score_start(sw_engine *e,uint32_t selected,uint32_t arrangement) {
 if(selected>=SW_SCORE_COUNT)return;
 e->score_selected=(uint16_t)selected;e->score_tick=e->score_fraction=e->score_event=0;e->score_playing=1;e->score_arrangement=(uint8_t)arrangement;e->title=0;
 if(selected==0&&arrangement)e->score_fraction=(e->bar*e->song.beats*96u+e->tick*48u)*256u;
}
void sw_command(sw_engine *e,uint32_t cmd,int32_t value) {
 switch(cmd) {
 case SW_PLAY:
  if(e->score_playing&&!e->score_arrangement)hush(e);
  e->playing=!e->playing;
  if(e->playing)e->title=0;
  if(!e->playing) {hush(e);record_cancel(e);e->walking=0;}
  else if(e->bar>=e->song.count) {e->bar=0;e->tick=0;e->tick_remaining=0;e->ending=0;}
  if(e->playing&&e->bar==0) {e->walking=1;e->walk_clock=0;}
  break;
 case SW_NEW:e->classic_mode=0;e->classic_request=0;sw_prepare(e,(uint32_t)value);break;
 case SW_LEON:
  e->title=0;
  if(!e->playing) {e->cue_kind=0;e->cue_position=0;}
  if(e->playing&&e->bar+5<e->song.count&&!e->leon_pending&&e->solo_until==0xffff)e->leon_pending=1;
  break;
 case SW_FILL:e->fill=1;break;
 case SW_STOPTIME:e->stop_bars=2;break;
 case SW_HOME:e->classic_cycle=2;e->home_pending=1;record_cancel(e);break;
 case SW_END:e->classic_cycle=2;e->ending=1;record_cancel(e);break;
 case SW_PLAYER:if(value>=0&&value<8) {e->players^=(uint8_t)(1u<<value);if(value==SW_SYNTH)e->synth_manual=1;}break;
 case SW_TEMPO:e->bpm=(uint16_t)clamp(value,80,220);
  #if SW_LEON_CLIP_FRAMES > 0
  if(e->record_active&&!e->playing)e->record_time_step=(float)SW_LEON_CLIP_FRAMES*(float)e->bpm/((float)SW_LEON_CLIP_BEATS*60.f*(float)SW_RATE);
  #endif
  break;
 case SW_SWING:e->swing=(uint16_t)clamp(value,50,68);break;
 case SW_HIPHOP:
  e->hiphop_target=clamp(value,0,100)*.01f;break;
 case SW_MODERN:
  if(!e->synth_manual&&e->modern_target==0&&value>0)e->players|=(1u<<SW_SYNTH);
  e->modern_target=clamp(value,0,100)*.01f;break;
 case SW_AIR:e->air=clamp(value,0,100)*.01f;break;
 case SW_GLOW:e->glow=clamp(value,0,100)*.01f;break;
 case SW_SWELL:e->swell=clamp(value,0,100)*.01f;break;
 case SW_FIDDLE_COLOUR:e->fiddle_colour=(uint8_t)clamp(value,0,100);break;
 case SW_KEY:e->key_next=(uint8_t)clamp(value,0,11);if(e->classic_mode)e->classic_request=1;break;
 case SW_FORM:e->form_next=(uint8_t)clamp(value,0,2);break;
 case SW_MASTER:e->master_target=clamp(value,0,100)*.01f;break;
 case SW_AUDITION:
  if(value>=0&&value<7) {
   /* Momentary sound check respects master/player mutes and never changes
      the tune, transport, random song seed or queued arrangement. */
   static const uint8_t pitches[7]={69,69,55,62,43,38,67};
   uint8_t kind=(uint8_t)(value==SW_DRUMS?SW_SNARE:value);
   foreground_note(e,(uint8_t)value,kind,pitches[value],value==SW_GUITAR?.12f:.26f,value==SW_GUITAR?7000:SW_RATE*3/4,0,value==SW_STEEL?-2:0);
   if(value==SW_BASS&&e->modern_target>0)acid_note(e,pitches[value],SW_RATE*3/4,0,1);
   if(value==SW_GUITAR||value==SW_PIANO)for(uint32_t n=1;n<3;n++) {
    sw_voice *stroke=foreground_note(e,(uint8_t)value,kind,(uint8_t)(pitches[value]+(n==1?4:7)),n==1?.10f:.09f,7000,0,0);
    if(stroke&&value==SW_GUITAR)stroke->onset_delay=(uint16_t)(n*110u);
   }
  }
  break;
 case SW_NOTE:e->title=0;foreground_note(e,SW_STEEL,SW_STEEL,(uint8_t)clamp(value,36,96),.28f,13230,-.1f,-2);break;
 case SW_BAND:e->players=(uint8_t)value;e->synth_manual=1;break;
 case SW_CUE:e->spoken=(uint8_t)(value!=0);if(!e->spoken)e->cue_position=0xffffffffu;break;
 case SW_HOLLER:call_pick(e,0);break;
 case SW_YEAH:call_pick(e,1);break;
 #ifdef SW_WITH_BANK
 case SW_SAMPLE_SELECT:
  if(value>=0&&(uint32_t)value<sw_sample_count())e->sample_selected=(uint16_t)value;
  break;
 case SW_SAMPLE_PLAY:
  {sw_bank_clip c;if(sample_clip(e->sample_selected,&c))sw_sample_start(&e->sample_player,&c,e->sample_selected);}
  break;
 case SW_SAMPLE_STOP:
  sw_sample_stop(&e->sample_player);
  break;
 case SW_CALL_FAMILY:if(value>=0&&(uint32_t)value<SW_CALL_POOLS)call_pick(e,(uint32_t)value);break;
 #endif
 case SW_RANDOM_CALL:call_pick(e,2);break;
 case SW_CALL_PAGE:e->call_page=(uint8_t)clamp(value,0,(int)(sw_call_count()-1)/16);break;
 case SW_SCORE_SELECT:if(value>=0&&(uint32_t)value<SW_SCORE_COUNT) {if(e->score_playing&&!e->score_arrangement)hush(e);e->score_choice=(uint16_t)value;}break;
 case SW_SCORE_PREVIEW:
  if(e->score_playing) {hush(e);break;}
  e->playing=0;record_cancel(e);hush(e);e->players|=1u<<sw_scores[e->score_choice].instrument;score_start(e,e->score_choice,0);break;
 case SW_CLASSIC_SELECT:
  if(value>=0&&(uint32_t)value<SW_CLASSIC_SLOTS) {e->classic_choice=(uint8_t)value;if(e->classic_mode&&sw_classic_available((uint32_t)value)) {e->key_next=classics[value].key;e->classic_request=1;}}
  break;
 case SW_CLASSIC_MODE:
  e->classic_mode=(uint8_t)(value!=0);e->pad_mode=0;
  if(e->classic_mode) {
   if(!sw_classic_available(e->classic_choice))for(uint32_t i=0;i<SW_CLASSIC_SLOTS;i++)if(sw_classic_available(i)) {e->classic_choice=(uint8_t)i;break;}
   if(sw_classic_available(e->classic_choice)) {e->key_next=classics[e->classic_choice].key;e->classic_request=1;}
  } else e->classic_request=2;
  break;
 case SW_SCORE_FEATURE:
  if(e->playing&&e->bar+6<e->song.count)for(uint32_t n=0;n<SW_AUTO_SCORE_COUNT;n++)if(sw_auto_scores[n]==e->score_choice) {
   uint32_t from=e->bar+1u+(e->tick>=e->song.beats*2u);
   if(e->score_choice!=0||tulsa_fits(e,from)) {record_cancel(e);e->score_scheduled=e->score_choice;}break;
  }
  break;
 case SW_SOURCE_SOLOS:e->source_solos=(uint8_t)(value!=0);if(!value) {e->score_scheduled=0xffff;if(e->score_arrangement)e->score_playing=0;}break;
 case SW_TEXTURE:e->texture=(uint8_t)clamp(value,0,100);break;
 case SW_TITLE:e->title=!e->title;break;
 case SW_AUTO_CALL:e->auto_calls=(uint8_t)(value!=0);break;
 case SW_CALL_PAD:if(value>=0&&(uint32_t)value<sw_call_count()) {e->title=0;e->cue_kind=(sw_call_index)value;e->cue_position=0;}break;
 case SW_PAD_MODE:e->pad_mode=(uint8_t)(value!=0);e->title=0;break;
 case SW_LEON_RECORD:
  if(sw_record_available()&&!e->record_active&&!e->record_pending) {e->title=0;if(!e->playing)record_start(e);else if(e->bar+6<e->song.count)e->record_pending=1;}
  break;
 default:break;
 }
 if(!e->playing&&e->pending_ready) {e->score_playing=0;record_cancel(e);adopt_pending(e);e->bar=e->tick=0;e->tick_remaining=0;e->song_revision++;e->ending=0;}
}
/* Voice storage is word-aligned. The alias-qualified word stores clear
   the entire object without creating a temporary or copying bytewise. */
typedef uint32_t sw_voice_word __attribute__((may_alias));
_Static_assert(sizeof(sw_voice)%sizeof(sw_voice_word)==0,"voice word size");
_Static_assert(_Alignof(sw_voice)>=_Alignof(sw_voice_word),"voice word alignment");
static void clear_voice(sw_voice *v) {
 sw_voice_word *word=(void *)v;
 for(unsigned i=0;i<sizeof *v/sizeof *word;i++)word[i]=0;
}

SW_RAM_AUDIO static void seed_steel_string(sw_engine *e,sw_voice *v,int16_t *delay,
                                           unsigned prefix,uint32_t increment,const float *harmonics) {
 #if defined(SW_TARGET_RAM_AUDIO)&&!defined(__APPLE__)
 #define SW_PICK_NOISE random32_audio
 #else
 #define SW_PICK_NOISE random32
 #endif
 for(unsigned j=0;j<prefix;j++) {
  float n=(float)(int32_t)SW_PICK_NOISE(&e->noise)*(1.f/2147483648.f);
  v->filter+=.45f*(n-v->filter);
 }
 uint32_t phase=increment*prefix;
 for(unsigned j=prefix;j<SW_STRING_SAMPLES;j++,phase+=increment) {
  float n=(float)(int32_t)SW_PICK_NOISE(&e->noise)*(1.f/2147483648.f);
  float pluck=harmonics?*harmonics++:steel_harmonics(phase);
  v->filter+=.45f*(n-v->filter);
  pluck=.82f*pluck+.18f*v->filter;
  delay[j]=(int16_t)((pluck+n*.018f)*28000.f);
 }
 #undef SW_PICK_NOISE
}

#if defined(SW_TARGET_RAM_AUDIO)&&!defined(__APPLE__)
__attribute__((section(".text.ram_bridge"),noinline))
static void seed_steel_bridge(sw_engine *e,sw_voice *v,int16_t *delay,
                              unsigned prefix,uint32_t increment,const float *harmonics) {
 void (*volatile target)(sw_engine *,sw_voice *,int16_t *,unsigned,uint32_t,const float *)=seed_steel_string;
 target(e,v,delay,prefix,increment,harmonics);
}
#else
#define seed_steel_bridge seed_steel_string
#endif

static void set_voice_pan(sw_voice *v,float pan) {
 v->pan=pan;
 v->pan_left=(1.f-pan)*.5f;
 v->pan_right=(1.f+pan)*.5f;
}
static sw_voice *note_ranked(sw_engine *e,uint8_t player,uint8_t type,uint8_t pitch,float amp,uint32_t gate,float pan,int bend,uint8_t foreground) {
 uint32_t i,slot=SW_ACTIVE_VOICES;float quiet=100;sw_voice *v;
 if(!(e->players&(1u<<player))||amp<.000001f)return 0;
 #ifdef SW_RENDER_PROFILE
 uint32_t note_mark=sw_profile_now(&e->profile);
 #endif
 /* Limit overlapping backing parts only between Then and Now.
    An arriving lead can always take a backing slot, while backing
    events never take a foreground slot under this limit. */
 if(e->modern>.0001f&&e->modern<.99995f) {
  uint32_t active=0,backing=SW_ACTIVE_VOICES;
  float score=100.f;
  for(i=0;i<SW_ACTIVE_VOICES;i++) {
   sw_voice *candidate=&e->voice[i];
   if(!candidate->active) {
    if(slot==SW_ACTIVE_VOICES)slot=i;
    continue;
   }
   active++;
   if(candidate->foreground)continue;
   float weight=candidate->envelope*candidate->amplitude;
   if(candidate->age>=candidate->gate)weight*=.25f;
   if(weight<score) {score=weight;backing=i;}
  }
  if(active>=14u) {
   if(backing!=SW_ACTIVE_VOICES&&(foreground||amp>score)) {
    slot=backing;
    e->voice_steals++;
   } else if(!foreground)return 0;
  }
 } else {
  for(i=0;i<SW_ACTIVE_VOICES;i++)if(!e->voice[i].active) {slot=i;break;}
 }
 if(slot==SW_ACTIVE_VOICES) {
  for(i=0;i<SW_ACTIVE_VOICES;i++)if(e->voice[i].envelope*e->voice[i].amplitude<quiet) {quiet=e->voice[i].envelope*e->voice[i].amplitude;slot=i;}
  e->voice_steals++;
 }
 v=&e->voice[slot];clear_voice(v);v->active=1;v->player=player;v->type=type;v->gate=gate;v->foreground=foreground;
 v->target=sw_pitch[pitch&127];v->increment=bend?sw_pitch[clamp(pitch+bend,0,127)]:v->target;
 v->bending=(uint8_t)(bend!=0);if(bend)v->bend_step=((float)v->target-(float)v->increment)/(float)SW_SLIDE_FRAMES;
 v->amplitude=amp;set_voice_pan(v,pan);v->life=gate+22050;v->envelope=1;
 v->decay= type==SW_PAD?.99999f:type==SW_ARP?.9998f:type==SW_STEEL?.999995f:type==SW_FIDDLE?.9999985f:type==SW_HORNS||type==SW_SYNTH?.99999f:type==SW_BASS?.99997f:type==SW_GUITAR?.99978f:type==SW_PIANO?.99994f:.9995f;
 if(type==SW_FIDDLE) {
  v->string_position=(uint16_t)(pitch<60?0:pitch<69?1:pitch<77?2:3);
  v->sample_scale=(float)SW_FIDDLE_RATE/((float)SW_RATE*(float)sw_pitch[sw_fiddle_root[v->string_position]]);
  /* Short bows are one-shot recordings, alternating without consuming song
     RNG. string_half is unused by the fiddle's sample renderer. */
  if(gate<SW_RATE/5&&pitch>=55&&pitch<=84) {
   v->string_half=(uint8_t)(1u+(e->notes_started[SW_FIDDLE]&1u));
   /* Medium bows join the sustained body rather than switching abruptly
      from a bouncing spiccato to a held orchestral note at200ms. */
   v->string_output=((float)SW_RATE/5.f-(float)gate)/((float)SW_RATE*.14f);
   if(v->string_output>1)v->string_output=1;
  }
  /* Both acoustic articulations already contain a recorded bow attack.
     Only de-click here; a second65ms swell makes held-note entries sluggish.
     The independent analogue envelope below still blooms in60ms. */
  v->string_period=176;
  v->string_target=.085f+10.f*(float)v->target*(1.f/4294967296.f);
  if(v->string_target>.40f)v->string_target=.40f;
  /* Analogue bow high-pass follows register as well as its low-pass. This
     state is unused by the acoustic sampler; no extra per-voice RAM. */
  v->string_previous=.006f+3.f*(float)v->target*(1.f/4294967296.f);
  if(v->string_previous>.035f)v->string_previous=.035f;
 }
 if(type==SW_HORNS) {
  v->string_target=.11f+(float)clamp(pitch-48,0,36)*.005f;
  v->string_position=(uint16_t)(pitch<71?0:1);
  v->sample_scale=(float)SW_REED_RATE/((float)SW_RATE*(float)sw_pitch[sw_reed_root[v->string_position]]);
 }
 if(type==SW_PIANO) {
  v->string_position=(uint16_t)(pitch<71?0:1);
  v->sample_scale=(float)SW_FIDDLE_RATE/((float)SW_RATE*(float)sw_pitch[sw_piano_root[v->string_position]]);
 }
 if(type==SW_STEEL||type==SW_GUITAR||type==SW_BASS) {
  v->string_half=(uint8_t)(type==SW_BASS||pitch+bend<45);
  float factor=v->string_half?.5f:1.f;
  float damping=type==SW_STEEL?.18f:.4f;
  v->string_period=4294967296.f/(float)v->increment*factor-damping;
  v->string_target=bend?4294967296.f/(float)v->target*factor-damping:v->string_period;
  if(v->string_period>510.f)v->string_period=510.f;
  if(v->string_target>510.f)v->string_target=510.f;
  if(bend)v->string_slide=(v->string_target-v->string_period)/(float)SW_SLIDE_FRAMES;
  uint32_t phase=0,increment=v->increment*(v->string_half?2u:1u);
  /* Bass never reads string_delay; advance to the same shared RNG state. */
  if(type==SW_BASS) e->noise=bass_noise_jump(e->noise);
  else {
   if(type==SW_GUITAR&&!v->bending) {
    /* The fixed-period reader starts in the tail and overwrites the prefix
       before wrapping back to it. Advance the shared noise and filter exactly.
       Include margin for rounding and the score's optional half-fret offset. */
    float reach=v->string_target*1.04f+2.f;
    uint32_t prefix=reach<512.f?512u-(uint32_t)reach:0u;
    for(uint32_t j=0;j<prefix;j++) {
     float n=(float)(int32_t)random32(&e->noise)*(1.f/2147483648.f);
     v->filter+=.32f*(n-v->filter);
    }
    phase=increment*prefix;
    for(uint32_t j=prefix;j<SW_STRING_SAMPLES;j++) {
     float n=(float)(int32_t)random32(&e->noise)*(1.f/2147483648.f);
     float pluck=.48f*osc(phase)+.18f*osc(phase*2)+.09f*osc(phase*3)+.04f*osc(phase*5);
     v->filter+=.32f*(n-v->filter);
     pluck=.48f*pluck+.32f*v->filter;
     e->string_delay[slot][j]=(int16_t)((pluck+n*.018f)*28000.f);
     phase+=increment;
    }
   } else if(type==SW_STEEL) {
    /* The tail bounds the delay read throughout the initial bend. */
    float longest=v->string_period>v->string_target?v->string_period:v->string_target;
    float reach=longest*1.04f+2.f;
    unsigned prefix=reach<512.f?512u-(unsigned)reach:0u;
    const float *harmonics=0;
    #ifdef SW_TARGET_PERIOD_CACHE
    unsigned start_pitch=bend?(unsigned)clamp(pitch+bend,0,127):pitch&127u;
    if(steel_cache_ready&&start_pitch>=SW_STEEL_CACHE_LOW&&start_pitch<=SW_STEEL_CACHE_HIGH) {
     unsigned index=start_pitch-SW_STEEL_CACHE_LOW;
     unsigned end=steel_offsets[index+1],available=end-steel_offsets[index];
     if(SW_STRING_SAMPLES-prefix<=available)harmonics=steel_cache+end-(SW_STRING_SAMPLES-prefix);
    }
    #endif
    seed_steel_bridge(e,v,e->string_delay[slot],prefix,increment,harmonics);
   } else {
    for(unsigned j=0;j<SW_STRING_SAMPLES;j++,phase+=increment) {
     float n=(float)(int32_t)random32(&e->noise)*(1.f/2147483648.f);
     float pluck=.48f*osc(phase)+.18f*osc(phase*2)+.09f*osc(phase*3)+.04f*osc(phase*5);
     v->filter+=.32f*(n-v->filter);
     pluck=.48f*pluck+.32f*v->filter;
     e->string_delay[slot][j]=(int16_t)((pluck+n*.018f)*28000.f);
    }
   }
  }
 }
 if(type==SW_BASS) {
  v->body2=pitch<34?0.f:1.f;
  v->sample_scale=(float)SW_UPRIGHT_RATE/((float)SW_RATE*(float)sw_pitch[sw_upright_root[(uint32_t)v->body2]]);
  v->decay=.999995f; /* The recorded pluck supplies its own natural decay. */
 }
 if(type==SW_GUITAR) {
  v->sample_bank=(uint8_t)(pitch<53?0:pitch<62?1:pitch<71?2:3);
  v->sample_scale=(float)SW_ARCHTOP_RATE/((float)SW_RATE*(float)sw_pitch[sw_archtop_root[v->sample_bank]]);
  /* The microphone recording supplies its own pluck/body decay. */
  v->decay=.999995f-.000215f*e->modern;
 }
 if(type==SW_KICK||type==SW_HAT)v->sample_bank=(uint8_t)(e->notes_started[SW_DRUMS]&1u);
 if(type>=SW_KICK&&type<=SW_OPEN_HAT) {v->decay=type==SW_KICK?.99925f:type==SW_SNARE?.9996f:type==SW_OPEN_HAT?.9991f:type==SW_CLAP?.9987f:.9975f;v->life=type==SW_OPEN_HAT?18000:13230;if(type==SW_KICK)v->increment=sw_pitch[48];if(type==SW_SNARE) {v->increment=sw_pitch[53];v->string_position=e->snare_take;e->snare_take^=1u;}}
 /* Recorded kick/hat provide a real body/tail. The electronic endpoint
    keeps its much shorter synthesized envelopes. */
 if(type==SW_KICK)v->decay=.99998f-.00073f*e->modern*(1.f-.75f*e->hiphop);
 if(type==SW_BELL) {v->decay=.9993f;v->life=8000;}
 if(type==SW_HAT)v->decay=.99998f-.00248f*e->modern;
 if(type==SW_GUITAR||type==SW_STEEL)v->filter=0;
 #ifdef SW_TRACE_BAND
 sw_trace_band_note(e,player,type,pitch,amp,gate,pan);
 #endif
 e->notes_started[player]++;
 #ifdef SW_RENDER_PROFILE
 e->profile.note_starts++;sw_profile_split(&e->profile,note_mark,&e->profile.ticks[SW_PROFILE_NOTE]);
 #endif
 return v;
}
void sw_key_off(sw_engine *e,uint32_t key) {
 if(key>=27)return;
 for(uint32_t i=0;i<SW_VOICES;i++)if(e->voice[i].active&&e->voice[i].manual_key==key+1u) {
  e->voice[i].gate=e->voice[i].age;e->voice[i].manual_key=0;
 }
}
void sw_keys_release(sw_engine *e) {for(uint32_t key=0;key<27;key++)sw_key_off(e,key);}
void sw_key_on(sw_engine *e,uint32_t key,uint32_t pitch,uint32_t player) {
 if(key>=27||pitch>96||pitch<36||player>=8||player==SW_DRUMS)return;
 sw_key_off(e,key);
 /* Band mutes govern automatic players; the chosen live instrument remains
    playable. Restore the exact mix before returning to the audio consumer. */
 uint8_t mix=e->players;e->players|=(uint8_t)(1u<<player);
 sw_voice *v=foreground_note(e,(uint8_t)player,(uint8_t)player,(uint8_t)pitch,.26f,SW_RATE*60u,-.1f,0);
 e->players=mix;if(v) {v->manual_key=(uint8_t)(key+1u);v->manual_voice=1;}
}
SW_RAM_AUDIO static float string_sample(sw_engine *e,uint32_t slot,sw_voice *v) {
 if(v->bending&&v->age<SW_SLIDE_FRAMES)v->string_period+=v->string_slide;
 else v->string_period=v->string_target;
 if(v->string_half&&(v->age&1u))return v->string_output;
 float period=v->string_period;if(v->type==SW_STEEL&&v->age>8000) {
  float depth=v->age>=14000u?1.f:(float)(v->age-8000u)/6000.f;
  period*=1.f-.003f*depth*osc_audio(v->age*487000u);
 }
 float position=(float)v->string_position-period;
 if(position<0)position+=SW_STRING_SAMPLES;
 uint32_t at=(uint32_t)position;float fraction=position-(float)at;
 /* Keep the delay and its previous sample in 16-bit sample units.
    The power-of-two scale is exact, so only the pickup output needs it. */
 int32_t a=e->string_delay[slot][at&511u],b=e->string_delay[slot][(at+1)&511u];
 float sample=(float)a+(float)(b-a)*fraction,damping=v->type==SW_STEEL?.18f:.4f;
 float filtered=(1.f-damping)*sample+damping*v->string_previous;
 float loss=v->type==SW_STEEL?.9994f:v->type==SW_BASS?.9975f:.996f;
 e->string_delay[slot][v->string_position]=(int16_t)(filtered*loss);
 v->string_previous=sample;v->string_output=sample*(1.f/32768.f);v->string_position=(v->string_position+1)&511u;
 return v->string_output;
}
static void advance_bar(sw_engine *e) {
 e->tick=0;e->bar++;
 if(e->score_scheduled!=0xffff&&e->score_scheduled!=0) {score_start(e,e->score_scheduled,1);e->score_scheduled=0xffff;}
 if(e->pending_ready) {
  record_cancel(e);
  adopt_pending(e);e->bar=0;e->song_revision++;e->ending=0;e->leon_pending=0;
  e->walking=1;e->walk_clock=0;
  e->solo_from=e->solo_until=0xffff;hush(e);
 }
 if(e->home_pending) {e->classic_cycle=2;e->score_playing=0;e->bar=4;e->home_pending=0;e->solo_from=e->solo_until=0xffff;e->leon_pending=0;e->ending=0;}
 if(e->ending==1) {e->score_playing=0;e->score_scheduled=0xffff;e->leon_pending=0;e->solo_from=e->solo_until=0xffff;e->bar=e->song.count-4;e->ending=2;}
 if(e->bar>=e->song.count) {e->playing=0;e->walking=0;hush(e);record_cancel(e);return;}
 if(e->record_active&&e->record_until!=0xffff&&e->bar>=e->record_until)record_cancel(e);
 if(e->record_pending)record_start(e);
 if(e->solo_until!=0xffff&&e->bar>=e->solo_until)e->solo_from=e->solo_until=0xffff;
 if(e->leon_pending&&(e->leon_pending==2||!e->source_solos||e->record_active||!tulsa_fits(e,e->bar+1))) {
  e->solo_from=e->bar;e->solo_until=(uint16_t)(e->bar+(e->leon_pending==2?tulsa_bars(e):8));
  if(e->solo_until>e->song.count-4)e->solo_until=e->song.count-4;
  e->leon_pending=0;e->callout=1;if(e->spoken) {e->cue_kind=0;e->cue_position=0;}
 }
 /* Sparse bandleader gestures at written handoffs/phrase starts. This RNG is
  * separate from planning and synthesis; no overlap with a manual call. */
 if(e->auto_calls&&!sw_call_active(e)&&e->bar>=4&&e->bar<e->song.count-4) {
  if(e->spoken&&(!e->score_playing||e->score_selected==0)&&e->song.bars[e->bar].section==SW_SOLO&&e->song.bars[e->bar-1].section!=SW_SOLO) {
   if(e->song.classic_id&&e->solo_from==0xffff&&e->bar<4u+classics[e->song.classic_id-1].bars&&classics[e->song.classic_id-1].lead!=SW_STEEL) {
    call_pick(e,0);if(e->cue_kind==0)call_pick(e,0);
    if(e->cue_kind==0)e->cue_position=0xffffffffu; /* A one-item custom pool must not announce Leon here. */
   }
   else {e->cue_kind=0;e->cue_position=0;}
  }
  else if(e->bar%8==4&&random32(&e->call_random)%3==0) {
   call_pick(e,2);
  }
 }
 if(e->stop_bars)e->stop_bars--;
}
/* The acid lane is monophonic and independent of the bounded band voices.
 * Glide carries phase/filter envelopes across tied steps; accents reopen the filter. */
static void acid_note(sw_engine *e,int pitch,uint32_t gate,int slide,int accent) {
 sw_acid *a=&e->acid;if(!(e->players&(1u<<SW_BASS)))return;
 float target=(float)sw_pitch[clamp(pitch,28,72)];
 a->sliding=(uint8_t)(slide&&a->active&&a->envelope>.02f);
 if(!a->sliding) {a->pitch=target;a->envelope=1.f;a->filter_envelope=1.f;}
 else {e->acid_slides++;if(accent)a->filter_envelope=1.f;}
 a->target=target;a->accent=accent?1.f:.62f;a->gate=gate;a->active=1;e->acid_notes++;e->notes_started[SW_BASS]++;
}
SW_RAM_AUDIO static float acid_sample(sw_engine *e) {
 sw_acid *a=&e->acid;if(!a->active)return 0;
 if(a->gate)a->gate--;else a->envelope*=.996f;
 if(!(e->players&(1u<<SW_BASS)))a->gate=0;
 if(a->envelope<.0001f) {a->active=0;return 0;}
 /* about 50 ms glide, near the 303's fixed slide time */
 a->pitch+=(a->target-a->pitch)*(a->sliding?.00045f:1.f);a->phase+=(uint32_t)a->pitch;
 a->sweep*=.99993f;if(a->sweep>1.f)a->sweep=1.f;
 float wave=saw(a->phase,(uint32_t)a->pitch);
 float pulse=.5f*(saw(a->phase,(uint32_t)a->pitch)-saw(a->phase+0x80000000u,(uint32_t)a->pitch));
 float input=.75f*wave+.25f*pulse;
 float cutoff=.014f+(.075f+.18f*e->modern)*a->filter_envelope*(.6f+.4f*a->accent)+.05f*a->sweep*a->accent;
 float v=saturate_audio((input-(2.1f+1.5f*e->modern)*a->pole[3])*(1.6f+1.4f*a->accent));
 for(uint32_t j=0;j<4;j++) {a->pole[j]+=cutoff*(v-a->pole[j]);v=a->pole[j];}
 a->filter_envelope*=a->accent>.9f?.9988f:.99925f;   /* accents snap shut faster */
 return (v*2.5f+.09f*input)*a->envelope*a->accent*.52f*e->modern;
}
/* Three-note rootless guitar shells, choosing the inversion/register nearest
 * the preceding shape. No RNG or progression changes; bass owns the root. */
static void guitar_shape(sw_engine *e,sw_chord c) {
 if(e->guitar_voiced&&e->guitar_chord.root==c.root&&e->guitar_chord.quality==c.quality)return;
 int best=10000;uint8_t chosen[3]={52,55,60};
 for(int inversion=0;inversion<3;inversion++)for(int octave=-1;octave<=1;octave++) {
  int candidate[3],cost=0;
  for(int i=0;i<3;i++) {
   int p=48+c.root+intervals[c.quality][1+(i+inversion)%3]+octave*12;
   while(i&&p<=candidate[i-1])p+=12;
   candidate[i]=p;
   int previous=e->guitar_voiced?e->guitar_voicing[i]:55+i*4;
   cost+=p>previous?p-previous:previous-p;
  }
  if(candidate[0]<48||candidate[2]>76)continue;
  if(cost<best) {best=cost;for(int i=0;i<3;i++)chosen[i]=(uint8_t)candidate[i];}
 }
 for(int i=0;i<3;i++)e->guitar_voicing[i]=chosen[i];
 e->guitar_chord=c;e->guitar_voiced=1;
}
/* Solo accompaniment keeps the third and color tone of the rootless shell. */
static uint32_t guitar_omit(const sw_engine *e,sw_chord chord) {
 if(e->bar>=e->song.count||(e->modern<=.0001f&&e->song.bars[e->bar].section!=SW_SOLO))return 3;
 uint32_t fifth=(chord.root+intervals[chord.quality][2])%12u;
 for(uint32_t i=0;i<3;i++)if(e->guitar_voicing[i]%12u==fifth)return i;
 return 2;
}
/* A new blended strum releases the preceding accompaniment shell like
   a rhythm guitarist changing grip. Manual and held notes retain their gates. */
static void guitar_stroke(sw_engine *e,sw_chord chord,float amp,uint32_t gate,uint32_t spread) {
 guitar_shape(e,chord);
 uint32_t omit=guitar_omit(e,chord);
 if(e->modern>.0001f&&e->modern<.99995f&&amp>=.000001f&&(e->players&(1u<<SW_GUITAR))) {
  for(uint32_t i=0;i<SW_ACTIVE_VOICES;i++) {
   sw_voice *v=&e->voice[i];
   if(v->active&&v->type==SW_GUITAR&&v->body2==1.f&&v->age>=v->gate) {
    v->decay=.95f;
    if(v->life>v->age+160u)v->life=v->age+160u;
    v->body2=0;
   }
  }
 }
 for(uint32_t i=0;i<3;i++)if(i!=omit) {
  sw_voice *v=note(e,SW_GUITAR,SW_GUITAR,e->guitar_voicing[i],amp,gate,-.42f,0);
  if(v) {
   v->onset_delay=(uint16_t)(i*spread);
   v->body2=1.f; /* Guitar's body2 is otherwise unused. */
  }
 }
}
static void fiddle_harmony(sw_engine *e,sw_chord chord,uint8_t melody,uint32_t gate) {
 uint8_t section=e->song.bars[e->bar].section;
 if(!e->texture||(section!=SW_THEME&&section!=SW_RETURN)||melody<58)return;
 static const uint8_t below[]={4,3,9,8};int harmony=-1;
 for(uint32_t i=0;i<4&&harmony<0;i++) {
  int p=(int)melody-below[i];if(p<55)continue;
  for(uint32_t n=0;n<4;n++)if(p%12==(chord.root+intervals[chord.quality][n])%12)harmony=p;
 }
 /* A passing melody tone can need a passing harmony. Prefer the song's
    diatonic third to a fourth/fifth or an out-of-range low violin note. */
 static const uint8_t major[]={0,2,4,5,7,9,11};
 for(uint32_t i=0;i<2&&harmony<0;i++) {
  int p=(int)melody-below[i];if(p<55)continue;
  for(uint32_t n=0;n<7;n++)if((p+12-e->song.key)%12==major[n])harmony=p;
 }
 if(harmony>=0) {
  sw_voice *v=foreground_note(e,SW_FIDDLE,SW_FIDDLE,(uint8_t)harmony,.14f*e->texture*.01f,gate,.18f,0);
  /* The partner has a rounder bow onset and a slightly warmer body. Keep
     pitch, sample clock, gate and RNG identical. sample_bank marks this
     fiddle only; bend_step is free because this voice is created unbent. */
  if(v) {v->sample_bank=1;v->string_period=352;}
 }
}
/* A phrase interval lands on the nearest tone of the chord: root, third, fifth (flat on diminished,
 * sharp on augmented) or the chord's sixth/seventh/ninth. Kept within the acid lane's range. */
static int acid_pitch(sw_chord c,uint32_t interval,int octave) {
 int fifth=c.quality==3?6:(c.quality==4||c.quality==8)?8:7;
 int tone[4]={0,intervals[c.quality][1],fifth,intervals[c.quality][3]%12},best=0,bestd=12;
 for(int k=0;k<4;k++) {int d=(int)interval-tone[k];if(d<0)d=-d;if(d>6)d=12-d;if(d<bestd) {bestd=d;best=tone[k];}}
 int pitch=36+c.root%12+best+12*octave;
 while(pitch>67)pitch-=12;
 while(pitch<36)pitch+=12;
 return pitch;
}
/* A crossfade still has one drummer. Combine simultaneous hits on the same
 * drum instead of allocating two voices for the identical onset. Delayed
 * roll strokes bypass this helper and keep their separate attack. */
static void groove_drum(sw_engine *e,uint8_t type,float amp,uint32_t gate,float pan) {
 if(!(e->players&(1u<<SW_DRUMS))||amp<.000001f)return;
 for(uint32_t i=0;i<SW_VOICES;i++) {
  sw_voice *v=&e->voice[i];
  if(v->active&&v->player==SW_DRUMS&&v->type==type&&!v->age&&!v->onset_delay) {
   float total=v->amplitude+amp;
   set_voice_pan(v,(v->pan*v->amplitude+pan*amp)/total);v->amplitude=total;
   if(gate>v->gate)v->gate=gate;
   return;
  }
 }
 note(e,SW_DRUMS,type,type==SW_KICK?36:type==SW_HAT?42:38,amp,gate,pan,0);
}
/* A source-aware 12-sixteenth bar: two groups of six in compound time,
 * three groups of four in the refrain. Existing 4/4/2/4/waltz settings keep
 * their exact old path. ACID phrase values are explicitly time-mapped from
 * sixteen source cells into twelve; this is an accompaniment adaptation. */
static void mixed_dance(sw_engine *e,uint32_t grouping,uint32_t step,uint32_t duration) {
 float m=e->modern,hh=e->hiphop,cf=hh<.5f?1.f-2.f*hh:0.f,gw=hh<.5f?2.f*hh:1.f;
 sw_chord c=e->dance_chord;uint32_t pulse=grouping==1?6u:4u,back=grouping==1?6u:8u;
 uint8_t section=e->song.bars[e->bar<e->song.count?e->bar:e->song.count-1].section;
 uint32_t melody=section==SW_THEME||section==SW_RETURN;
 if(m>.0001f&&hh<.5f) {
  const uint8_t *pair=sw_acid_pair[e->song.seed%SW_ACID_PAIRS];
  uint32_t w=sw_acid_phrase[pair[e->bar%4u==3u?1u:0u]][step*4u/3u];
  if((step%pulse==0||m>.18f)&&(w&1u)) {
   uint32_t out=(w>>8)&1u,accent=(w>>7)&1u;
   acid_note(e,acid_pitch(c,(w>>1)&15u,(int)((w>>5)&3u)-1),duration*(out?16u:(w>>9)&1u?9u:5u)/10u,e->acid.prev_slide,(int)accent);
   e->acid.prev_slide=(uint8_t)out;if(accent)e->acid.sweep+=.18f;
  } else e->acid.prev_slide=0;
 }
 if(e->dance_final)return;
 if(m>.0001f&&cf>.0001f&&!(step&1u))groove_drum(e,SW_HAT,(step%pulse==0u?.045f:.025f)*m*cf,1600,.35f);
 if(hh>.0001f) {
  if(step==0u||step==pulse||(e->bar%4u==3u&&step==11u)) {groove_drum(e,SW_KICK,.30f*gw,9000,0);if(m>0&&(e->players&(1u<<SW_DRUMS)))e->pump=1;}
  if(step==back) {groove_drum(e,SW_SNARE,.22f*gw,7000,-.1f);groove_drum(e,SW_CLAP,.13f*gw,5000,.12f);}
  groove_drum(e,SW_HAT,(step%pulse==0?.038f:(step&1u)?.018f:.028f)*gw,1400,.35f);
  if(hh>=.5f&&(step==0u||step==pulse||step==10u)) {
   int fifth=c.quality==3?6:(c.quality==4||c.quality==8)?8:7;
   int p=(c.root%12>=7?24:36)+c.root+(step==10u?fifth:step==pulse?12:0);
   if(m<.99999f)note(e,SW_BASS,SW_BASS,(uint8_t)p,.43f,duration*2u,-.04f,0);
   if(m>.0001f) {e->acid.prev_slide=0;acid_note(e,p<36?p+12:p,duration*18u/10u,0,step%pulse==0);}
  }
 }
 if(step%pulse==pulse-2u) {
  note(e,SW_DRUMS,SW_OPEN_HAT,42,.04f*m*cf,duration*2u,.3f,0);
  guitar_stroke(e,c,.075f*m*(hh>=.5f?hh:cf),duration,0);
 }
 if(step==back)groove_drum(e,SW_CLAP,.14f*m*cf,5000,-.12f);
 if(!melody&&(e->players&(1u<<SW_SYNTH))&&(!e->record_active||m<.65f)) {
  if(step%2u==0u) {note(e,SW_SYNTH,SW_ARP,closest(c,72+(int)((step/2u+e->bar)%4u)*3,65,91),.10f*m,duration*2u/3u,step&2u?.3f:-.3f,0);e->arp_notes++;}
 }
}
/* Sixteenth subdivisions share the band's exact long/short eighth boundary.
 * Swing50 straightens both; no independent clock accumulates timing error. */
static void dance_step(sw_engine *e) {
 uint32_t sub=e->dance_sub++,q=e->dance_quarter,step=q*4+sub;
 float m=e->modern,hh=e->hiphop,cf=hh<.5f?1.f-2.f*hh:0.f,gw=hh<.5f?2.f*hh:1.f;if((m<.0001f&&hh<.0001f)||!e->dance_allowed||(e->dance_final&&sub)||(e->stop_bars&&(q||sub)))return;
 sw_chord c=e->dance_chord;uint32_t duration=e->dance_length/4;e->dance_steps++;
 uint32_t grouping=classic_grouping(e);
 if(grouping) {mixed_dance(e,grouping,step,duration);return;}
 /* Acid line: a pair of phrases from the ACID bank chosen by the song's seed, the first for three
  * bars and the second every fourth. Each keeps its rhythm, accents, slides, gates and octave moves;
  * its intervals land on the current chord's tones. Slides follow the 303: the mark is on the
  * outgoing step, which holds into the next note; the next note glides in without a new pluck. */
 if(m>.0001f&&hh<.5f&&(sub==0||m>.18f)) {
  const uint8_t *pair=sw_acid_pair[e->song.seed%SW_ACID_PAIRS];
  uint32_t w=sw_acid_phrase[pair[e->bar%4u==3u?1u:0u]][step];
  if(w&1u) {
   uint32_t out=(w>>8)&1u,accent=(w>>7)&1u;
   acid_note(e,acid_pitch(c,(w>>1)&15u,(int)((w>>5)&3u)-1),duration*(out?16u:(w>>9)&1u?9u:5u)/10u,e->acid.prev_slide,(int)accent);
   e->acid.prev_slide=(uint8_t)out;if(accent)e->acid.sweep+=.18f;
  }
  else e->acid.prev_slide=0;
 }
 /* HIP-HOP: early-1980s electro. Kick on 1, the "and" of 2 and the "and" of 3, a push
  * before every fourth bar; snare and clap on 2 and 4, hard and sparse; sixteenth hats
  * with off-beat accents and a roll into every fourth bar. The kit is whatever
  * Then->Now says: the acoustic kit at Then, the synth kit with an 808 boom and cowbell at Now. */
 if(hh>.0001f&&!e->dance_final) {
  uint32_t s16=(e->song.beats==2?(e->bar&1u)*8u+step:step)&15u,roll=e->bar%4u==3u;
  if(s16==0u||s16==6u||s16==10u||(roll&&s16==14u)) {groove_drum(e,SW_KICK,.32f*gw,9000,0);if(m>0&&(e->players&(1u<<SW_DRUMS)))e->pump=1;}
  if(s16==4u||s16==12u) {groove_drum(e,SW_SNARE,.24f*gw,7000,-.1f);groove_drum(e,SW_CLAP,.14f*gw,5000,.12f);}
  groove_drum(e,SW_HAT,((s16&1u)?.018f:(s16&2u)?.036f:.026f)*gw,1400,.35f);
  if(roll&&s16>=13u) {sw_voice *r2=note(e,SW_DRUMS,SW_HAT,42,.022f*gw,1200,.38f,0);if(r2)r2->onset_delay=(uint16_t)(duration/2u);}
  if(m>.3f&&(s16==3u||s16==10u))note(e,SW_DRUMS,SW_BELL,76,.05f*gw*m,6000,.25f,0);
  if(hh>=.5f) {
   /* electro bass: root, a pickup, the octave on the "and" of 2, root and fifth late in the bar */
   static const int8_t line[16]={0,-1,-1,0,-1,-1,12,-1,0,-1,0,7,-1,-1,-1,-1};
   int iv=line[s16];
   if(iv>=0) {
    int fifth=c.quality==3?6:(c.quality==4||c.quality==8)?8:7,low=c.root%12>=7?24:36,p=low+c.root+(iv==7?fifth:iv);
    if(m<.99999f)note(e,SW_BASS,SW_BASS,(uint8_t)p,.46f,duration*(s16==0u?3u:1u),-.04f,0);
    if(m>.0001f) {e->acid.prev_slide=0;acid_note(e,p<36?p+12:p,duration*(s16==0u?3u:1u)*9u/10u,0,s16==0u||s16==6u);}
   }
   /* guitar: short chord stabs instead of strums */
   if(s16==6u||s16==14u) {guitar_stroke(e,c,.10f*hh,duration,0);}
  }
 }
 if(e->dance_final)return;
 /* under the melody the hats keep only the off-beat; sixteenths return in intros, bridges and solos */
 uint8_t section=e->song.bars[e->bar<e->song.count?e->bar:e->song.count-1].section;uint32_t melody=section==SW_THEME||section==SW_RETURN;
 if(sub) {
  if(sub==2||!melody)note(e,SW_DRUMS,sub==2?SW_OPEN_HAT:SW_HAT,42,(sub==2?.045f:.032f)*m*cf,sub==2?duration*2:1600,.3f,0);
  if(m>.65f&&(step==7||((e->fill||e->bar%8==7)&&step==15))) {note(e,SW_DRUMS,SW_KICK,36,.11f*m*cf,5000,0,0);if(e->players&(1u<<SW_DRUMS))e->pump=1;}
 }
 if(sub==0&&(q&1)) {
  if(hh>0)groove_drum(e,SW_CLAP,.16f*m*cf,5000,-.12f);
  else note(e,SW_DRUMS,SW_CLAP,38,.16f*m,5000,-.12f,0);
 }
 if(sub==2) {guitar_stroke(e,c,.085f*m*cf,duration,0);}
 if(!melody&&(e->players&(1u<<SW_SYNTH))&&(!e->record_active||m<.65f)) {
  if(sub==0||sub==2) {note(e,SW_SYNTH,SW_ARP,closest(c,72+(int)((step+e->bar)%4)*3,65,91),.14f*m,duration*2/3,.3f*(sub&1?1:-1),0);e->arp_notes++;}
 }
}
static void lead_dispatch(sw_engine *e,uint32_t t) {
 sw_bar *bar=&e->song.bars[e->bar];
 if(e->song.classic_id&&(bar->section==SW_THEME||bar->section==SW_RETURN)&&!(e->solo_from!=0xffff&&e->bar>=e->solo_from&&e->bar<e->solo_until))return;
 if(!(bar->lead_mask&(1u<<t))||e->record_active||e->score_playing)return;
 int solo=(e->solo_from!=0xffff&&e->bar>=e->solo_from&&e->bar<e->solo_until)||bar->section==SW_SOLO;
 uint8_t lead=solo?SW_STEEL:(e->bar/8)%2?SW_STEEL:SW_FIDDLE;
 if(e->song.classic_id&&bar->section==SW_SOLO&&e->bar<4u+classics[e->song.classic_id-1].bars&&e->solo_from==0xffff)lead=classics[e->song.classic_id-1].lead;
 if(!(e->players&(1u<<lead)))lead=lead==SW_STEEL?SW_FIDDLE:SW_STEEL;
 uint32_t gate=bar->gate[t]*SW_RATE*60u/(e->bpm*4u);if(gate<800)gate=800;
 foreground_note(e,lead,lead,bar->lead[t],.24f,gate,-.12f,lead==SW_STEEL&&!(t%8)&&!(e->bar&1)?-2:0);
 if(lead==SW_FIDDLE)fiddle_harmony(e,bar->chord[t/8],bar->lead[t],gate);
}
/* Skip only frames before the next exact integer-grid threshold. The caller
 * keeps this wait local to one render block and resets it at a band tick. */
static uint32_t grid_wait_frames(const sw_engine *e,uint32_t base,uint32_t target) {
 const uint32_t step=48u*256u,length=e->tick_length,remaining=e->tick_remaining;
 if(!length||remaining>length||length>(UINT32_MAX-(step-1u))/step||target<=base)return 0;
 uint32_t delta=target-base;
 if(delta>=step)return remaining?remaining-1u:0;
 uint32_t at=(delta*length+step-1u)/step,elapsed=length-remaining;
 return at>elapsed?at-elapsed-1u:0;
}
static uint32_t score_render_tick(sw_engine *e) {
 if(!e->score_playing)return 0;
 const sw_score *score=&sw_scores[e->score_selected];
 uint32_t tulsa=e->score_arrangement&&e->score_selected==0,position=0;
 uint32_t next_position=0;
 if(tulsa)next_position=e->score_fraction+(score->end*256u+9u)/10u;
 if(tulsa) {position=band_position(e);e->score_tick=position>=e->score_fraction?(position-e->score_fraction)*10u/256u:0;}
 while(e->score_event<score->count) {
  const sw_score_event *next=&score->events[e->score_event];
  uint32_t target=tulsa?e->score_fraction+next->at*256u/10u:next->at;
  if(tulsa&&(next->flags&1u))target=classic_warp(target,e->swing,1);
  if(target>(tulsa?position:e->score_tick)) {
   if(tulsa&&target<next_position)next_position=target;
   break;
  }
  const sw_score_event *event=&score->events[e->score_event++];
  int pitch=event->pitch;if(e->score_arrangement) {int delta=(int)e->song.key-(int)score->key;if(delta>6)delta-=12;if(delta<-6)delta+=12;pitch+=delta;}
  uint32_t gate=(uint32_t)((float)event->duration*(60.f*SW_RATE/960.f)/(float)e->bpm);
  if(tulsa) {
   uint32_t end=e->score_fraction+(event->at+event->duration)*256u/10u;
   uint32_t end_time=event->flags&2u?end:classic_warp(end,e->swing,0),now=classic_warp(position,e->swing,0);
   gate=(uint32_t)((float)(end_time>now?end_time-now:1)*(60.f*SW_RATE/(96.f*256.f))/(float)e->bpm);
  }
  foreground_note(e,score->instrument,score->instrument,(uint8_t)clamp(pitch,0,127),.24f*event->velocity/90.f,gate,-.12f,0);e->score_notes++;
  #ifdef SW_TRACE_SCORES
  sw_trace_score_note(e->score_selected,event->at,event->duration,(uint8_t)clamp(pitch,0,127),event->flags);
  #endif
 }
 if(!tulsa) {e->score_fraction+=e->bpm*960u;
  if(e->score_fraction>=SW_RATE*60u) {e->score_tick+=e->score_fraction/(SW_RATE*60u);e->score_fraction%=SW_RATE*60u;}
 }
 if(e->score_tick>=score->end)e->score_playing=0;
 if(!tulsa||!e->score_playing||!e->tick||!e->tick_length)return 0;
 uint32_t base=(e->bar*e->song.beats*96u+(e->tick-1)*48u)*256u;
 return grid_wait_frames(e,base,next_position);
}
/* Canonical swung grid <-> elapsed quarter position, both at96PPQ/Q8.
 * Triplet onsets use the straight clock; ordinary eighths follow the band. */
static uint32_t classic_warp(uint32_t position,uint32_t swing,uint32_t inverse) {
 uint32_t quarter=96u*256u,half=48u*256u,base=position/quarter*quarter,at=position%quarter,split=quarter*swing/100u;
 if(inverse)return base+(at<split?at*half/split:half+(at-split)*half/(quarter-split));
 return base+(at<half?at*split/half:split+(at-half)*(quarter-split)/half);
}
static uint32_t classic_render_tick(sw_engine *e) {
 if(!e->playing||!e->song.classic_id||!e->tick||!e->tick_length)return 0;
 const sw_classic *classic=&classics[e->song.classic_id-1];
 /* Mixed settings use one straight source clock in both meters. Avoid a
    source-phase lookup for every sample; grouping changes only the band. */
 uint32_t swing=classic->compound_bars?50u:e->swing;
 uint32_t bar_ticks=e->song.beats*96u;
 uint32_t clock_base=(e->bar*bar_ticks+(e->tick-1)*48u)*256u;
 uint32_t position=clock_base+(e->tick_length-e->tick_remaining)*(48u*256u)/e->tick_length;
 uint32_t start=4*bar_ticks-classic->pickup,ret=(4+classic->bars+classic->solo)*bar_ticks-(classic->ret_start?0:classic->pickup);
 uint32_t head_start=start/96u*96u*256u,head_end=(4*bar_ticks+classic->body)*256u;
 uint32_t return_start=ret/96u*96u*256u,return_end=(4+classic->bars+classic->solo+classic->ret)*bar_ticks*256u;
 uint32_t base,cycle,limit;
 if(position>=head_start&&position<head_end) {cycle=0;base=start;limit=head_end;}
 else if(position>=return_start&&position<return_end) {
  cycle=1;base=ret-(classic->ret_start?classic->pickup+classic->ret_start*bar_ticks:0);limit=return_end;
 } else {
  e->classic_cycle=2;
  uint32_t next=position<head_start?head_start:UINT32_MAX;
  if(position<return_start&&return_start<next)next=return_start;
  return grid_wait_frames(e,clock_base,next);
 }
 if(e->classic_cycle!=cycle) {e->classic_offset=e->classic_at=0;e->classic_cycle=(uint8_t)cycle;}
 while(e->classic_offset<classic->bytes) {
  uint32_t next=e->classic_offset;uint16_t word;
  if(!classic_read_word(classic,&next,&word)) {e->classic_offset=classic->bytes;break;}
  uint32_t flags=word&0x3000u,target=(base+e->classic_at)*256u;
  if(flags&0x1000u)target=classic_warp(target,swing,1);
  if(target>position)return grid_wait_frames(e,clock_base,target<limit?target:limit);
  uint8_t pitches[8];int8_t cents[8];uint32_t count=0,duration=0;
  do {e->classic_offset=next;uint32_t code=(word>>7)&31u;
   if((word&127)&&count<8) {pitches[count]=(uint8_t)(word&127);cents[count++]=(word&0x4000u)?50:(word&0x8000u)?-50:0;}
   if(code==31u) {
    uint16_t literal;
    if(!classic_read_word(classic,&e->classic_offset,&literal)) {e->classic_offset=classic->bytes;return 0;}
    duration=literal;
   } else {
    if(code>=sizeof classic_durations/sizeof classic_durations[0]) {e->classic_offset=classic->bytes;return 0;}
    duration=classic_durations[code];
   }
   if(duration||e->classic_offset>=classic->bytes)break;
   next=e->classic_offset;
   if(!classic_read_word(classic,&next,&word)) {e->classic_offset=classic->bytes;return 0;}
  } while(!duration);
  if(!duration)break;
  uint32_t end=(base+e->classic_at+duration)*256u,end_grid=(flags&0x2000u)?classic_warp(end,swing,1):end;
  if(end_grid>position&&!e->record_active&&!e->score_playing&&!(e->solo_from!=0xffff&&e->bar>=e->solo_from&&e->bar<e->solo_until)) {
   uint8_t player=classic->lead;if(!(e->players&(1u<<player)))player=player==SW_STEEL?SW_FIDDLE:SW_STEEL;
   int delta=(int)e->song.key-classic->key;if(delta>6)delta-=12;if(delta<-6)delta+=12;
   uint32_t end_time=(flags&0x2000u)?end:classic_warp(end,swing,0),now=classic_warp(position,swing,0);
   uint32_t gate=(uint32_t)((float)(end_time>now?end_time-now:1)*(60.f*SW_RATE/(96.f*256.f))/(float)e->bpm*.94f);
   for(uint32_t n=0;n<count;n++) {
    sw_voice *v=foreground_note(e,player,player,(uint8_t)clamp(pitches[n]+delta,24,108),.22f/(count>1?1.35f:1.f),gate,n? .15f:-.15f,0);
    if(v&&cents[n]) {
     /* Printed half-fret steel sonorities retain their actual pitch. Both
        oscillator and physical-string delay tune together, without pow(). */
     float ratio=cents[n]>0?1.0293022366f:.9715319412f;
     v->increment=(uint32_t)((float)v->increment*ratio);v->target=v->increment;
     if(player==SW_STEEL||player==SW_GUITAR||player==SW_BASS) {float damping=player==SW_STEEL?.18f:.4f;v->string_period=(v->string_period+damping)/ratio-damping;if(v->string_period>510.f)v->string_period=510.f;v->string_target=v->string_period;}
    }
    e->classic_notes++;
    #ifdef SW_TRACE_CLASSICS
    sw_trace_classic_note(e->song.classic_id-1,e->classic_at,duration,(uint8_t)clamp(pitches[n]+delta,24,108),cents[n]);
    #endif
   }
   /* Original harmony is accompaniment: keep it outside the written-source
      trace/export and do not add a third voice to an existing source dyad. */
   if(player==SW_FIDDLE&&count==1&&!cents[0]) {
    uint32_t at=e->classic_at>classic->pickup?e->classic_at-classic->pickup:0;
    uint8_t packed=classic_chord_at(classic,(at/48u)%(classic->body/48u));
    sw_chord chord={(uint8_t)((packed%16+delta+12)%12),(uint8_t)(packed>>4)};
    fiddle_harmony(e,chord,(uint8_t)clamp(pitches[0]+delta,24,108),gate);
   }
  }
  e->classic_at+=duration;
 }
 return grid_wait_frames(e,clock_base,limit);
}
static void dispatch(sw_engine *e) {
 sw_bar *bar;sw_chord c;uint32_t t,q,quarter,gate;int solo,lead,pitch,finalbar;
 if(e->tick==e->song.beats*2u)advance_bar(e);
 if(!e->playing)return;
 bar=&e->song.bars[e->bar];t=e->tick;q=t/2;c=bar->chord[t/4];quarter=SW_RATE*60/e->bpm;
 if(e->record_active&&!e->record_release) {c.root=e->song.key;c.quality=0;}
 solo=(e->solo_from!=0xffff&&e->bar>=e->solo_from&&e->bar<e->solo_until)||bar->section==SW_SOLO;
 if(e->song.classic_id&&e->bar>=4&&e->bar<e->song.count-4) {
  const sw_classic *classic=&classics[e->song.classic_id-1];uint32_t phase=e->bar-4;
  if(bar->section==SW_SOLO)phase=phase-classic->bars+classic->solo_start;
  if(bar->section==SW_RETURN)phase=phase-classic->bars-classic->solo+classic->ret_start;
  uint8_t packed=classic_chord_at(classic,(phase*e->song.beats*2+t)%(classic->body/48));
  c.root=(uint8_t)((packed%16+(int)e->song.key-classic->key+12)%12);c.quality=packed>>4;
 }
 finalbar=e->bar==e->song.count-1;
 uint32_t grouping=classic_grouping(e);
 if(t+1==e->song.beats*2u&&!e->record_active) {
  uint32_t from=e->bar+1,full=tulsa_fits(e,from);
  if(full&&((e->leon_pending==1&&e->source_solos)||e->score_scheduled==0)) {
   uint32_t manual=e->leon_pending==1;score_start(e,0,1);e->score_scheduled=0xffff;
   if(manual)e->leon_pending=2;
   else {e->solo_from=(uint16_t)from;e->solo_until=(uint16_t)(from+tulsa_bars(e));}
  }
  if(e->source_solos&&!e->score_playing&&from<e->song.count&&e->song.bars[from].section==SW_SOLO&&bar->section!=SW_SOLO) {
   if(!e->song.classic_id) {
    uint32_t selected=sw_auto_scores[(e->song.seed+e->bar)%SW_AUTO_SCORE_COUNT];
    if(!selected) {if(full)score_start(e,0,1);}else e->score_scheduled=(uint16_t)selected;
   } else if(e->song.classic_id==3&&full)score_start(e,0,1);
  }
 }
 if(e->score_playing&&e->score_arrangement) {
  /* Full pickup-inclusive printed G/D7/G/D7/G clock; both necks. */
  if(e->score_selected==0)c=tulsa_chord(e,e->score_tick);
  else {c.root=e->song.key;c.quality=0;}
 }

 if(!(t&1)) {
  e->dance_clock=0;e->dance_length=(SW_RATE*6000u+e->clock_remainder)/(e->bpm*100u);
  e->dance_quarter=(uint8_t)q;e->dance_sub=0;e->dance_chord=c;e->dance_final=(uint8_t)finalbar;
  e->dance_allowed=(uint8_t)((!finalbar||t==0)&&(!e->stop_bars||t==0));
 }
 if(grouping)e->dance_chord=c; /* also adopt the second 6/8 chord at eighth3 */
 if(grouping&&(!finalbar||t==0)&&(!e->stop_bars||t==0)) {
  float hh=e->hiphop,cf=hh<.5f?1.f-2.f*hh:0.f,texture=e->texture*.01f;
  float space=bar->section==SW_SOLO?1.f-.22f*texture:1.f;
  uint32_t pulse=grouping==1?3u:2u;
  if(t%pulse==0u) {
   uint32_t group=t/pulse,hot=bar->section==SW_SOLO;
   uint32_t length=grouping==1?quarter*3u/2u:quarter;
   int fifth=c.quality==3?6:(c.quality==4||c.quality==8)?8:7;
   int bass=(c.root%12>=7?24:36)+c.root;
   if(grouping==1&&group)bass+=fifth;
   else if(hot&&group)bass+=group==1?intervals[c.quality][1]:fifth;
   if(e->modern<.99999f&&hh<.5f&&(grouping==1||!group||hot))note(e,SW_BASS,SW_BASS,(uint8_t)bass,.40f,length*4u/5u,-.04f,0);
   guitar_stroke(e,c,.08f*(group?1.f:.75f)*(1.f-e->modern)*cf*space,length/3u,110);
   if(group&&(bar->section!=SW_SOLO||!e->texture))for(uint32_t i=1;i<3;i++)note(e,SW_PIANO,SW_PIANO,(uint8_t)(60+c.root+intervals[c.quality][i]),.05f*(1.f-.65f*e->modern)*cf,length/3u,.4f,0);
   groove_drum(e,SW_KICK,((group?.065f:.105f)+.18f*e->modern)*cf,6500,0);
   if(e->modern>0&&(e->players&(1u<<SW_DRUMS)))e->pump=1;
   if(group)groove_drum(e,SW_SNARE,(.09f+.12f*e->modern)*cf,5000,-.1f);
  }
  if(!finalbar)groove_drum(e,SW_HAT,(t%pulse==0u?.036f:.022f)*(1.f-e->modern)*cf,1600,.35f);
 }
 if(!grouping&&!(t&1)&&(!finalbar||t==0)&&(!e->stop_bars||t==0)) {
  float texture=e->texture*.01f;float space=bar->section==SW_SOLO?1.f-.22f*texture:1.f;
  float hh=e->hiphop,cf=hh<.5f?1.f-2.f*hh:0.f;   /* the country groove is gone by half-way; the hip-hop groove (dance_step) is in */
  /* Two2/4 bars share a four-quarter walking phrase, rather than
     forcing a chromatic approach on every other beat. */
  uint32_t walk=e->song.beats==2?(e->bar&1u)*2u+q:q;
  uint32_t two=bar->section==SW_INTRO||bar->section==SW_THEME;
  uint32_t cell=(e->song.seed+e->bar*7u)%3u;
  /* Upright register: roots C2-F#2 as before; G-B drop an octave to G1-B1, where an upright's
     low strings carry the weight. The fifth follows the chord: flat on diminished (quality 3),
     sharp on augmented (4, 8), perfect otherwise. */
  int low=c.root%12>=7?24:36,bass=low+c.root,fifth=c.quality==3?6:(c.quality==4||c.quality==8)?8:7;
  if(two) {if(walk==2)bass+=fifth;}
  else {
   if(walk==1)bass+=cell==0?intervals[c.quality][1]:cell==1?2:fifth;
   /* a dominant seventh walks through its flat seventh on beat 3 unless the cell takes the third */
   if(walk==2)bass+=cell==1?intervals[c.quality][1]:(cell==0&&c.quality!=1)?fifth:intervals[c.quality][3];
   if(q+1==e->song.beats&&(e->song.beats!=2||(e->bar&1u))) {
    sw_chord next=e->song.bars[(e->bar+1)%e->song.count].chord[0];if(e->record_active&&e->bar+1<e->record_until)next.root=e->song.key;if(e->score_playing&&e->score_arrangement&&e->score_selected==0)next=tulsa_next_chord(e);
    /* lead into a real change by a semitone; on a held chord, step to the fifth (or the root
       after a fifth) instead of approaching the chord it is already on */
    if(next.root!=c.root||next.quality!=c.quality) {int nlow=next.root%12>=7?24:36;bass=nlow+next.root+(cell==1?1:-1);}
    else bass=low+c.root+(cell==2?0:fifth);
   }
  }
  if(e->modern<.99999f&&hh<.5f&&(!two||!(q&1)))note(e,SW_BASS,SW_BASS,(uint8_t)bass,.43f,two?quarter*5/4:quarter*3/4,-.04f,0);
  guitar_stroke(e,c,.085f*(q&1?1.12f:.9f)*(1.f-e->modern)*space*cf,quarter/3,110);
  if((q&1)&&(bar->section!=SW_SOLO||!e->texture))for(uint32_t i=1;i<3;i++)note(e,SW_PIANO,SW_PIANO,(uint8_t)(60+c.root+intervals[c.quality][i]),.055f*(1.f-.65f*e->modern)*(1.f-.7f*hh),quarter/3,.4f,0);
  note(e,SW_DRUMS,SW_KICK,36,((.27f+e->modern*.15f)-(q==0?.15f:.17f)*(1.f-e->modern))*cf,6500,0,0);if(e->modern>0&&(e->players&(1u<<SW_DRUMS)))e->pump=1;
  if(q&1)note(e,SW_DRUMS,9,38,(.16f+e->modern*.13f)*cf,6000,-.1f,0);
 }
 if(!grouping&&!finalbar&&(!e->stop_bars||t==0)) {
  note(e,SW_DRUMS,10,42,((t&1)?.032f:.052f)*(1.f-e->modern)*(e->hiphop<.5f?1.f-2.f*e->hiphop:0.f),1600,.35f,0);
  if((e->fill||e->bar%8==7)&&t>=6)note(e,SW_DRUMS,9,38,.075f*(e->hiphop<.5f?1.f-2.f*e->hiphop:0.f),3000,-.2f+.2f*(t-6),0);
 }
 (void)solo;(void)lead;(void)pitch;(void)gate;
 lead_dispatch(e,t*2);
 if(e->texture&&bar->section==SW_BRIDGE&&t==6)for(uint32_t i=1;i<3;i++)note(e,SW_PIANO,SW_PIANO,(uint8_t)(60+c.root+intervals[c.quality][i]),.055f,quarter/3,.4f,0);
 if((e->players&(1u<<SW_HORNS))&&t==0&&(bar->section==SW_RETURN||(bar->section==SW_THEME&&!(e->bar%4))))note(e,SW_HORNS,SW_HORNS,closest(c,67,55,79),.11f,quarter,.25f,0);
 if(t==7)e->fill=0;
 if(grouping&&t==5)e->fill=0;
 {uint32_t swing=grouping?50u:e->swing;
  uint32_t numerator=SW_RATE*60u*((t&1)?100u-swing:swing)+e->clock_remainder;
  uint32_t denominator=e->bpm*100u;e->tick_remaining=numerator/denominator;e->clock_remainder=numerator%denominator;}
 #if SW_LEON_CLIP_FRAMES > 0
 if(e->record_active)e->record_time_step=(float)SW_LEON_CLIP_FRAMES*(float)((t&1)?100u-e->swing:e->swing)/((float)SW_LEON_CLIP_BEATS*100.f*(float)e->tick_remaining);
 #endif
 e->tick_length=e->tick_remaining;e->lead_second=1;e->lead_wait=e->tick_remaining/2;
 if(!(t&1))e->dance_split=e->tick_remaining;
 e->tick++;
}
#ifdef SW_RENDER_PROFILE
SW_RAM_AUDIO static uint32_t profile_voice_scope(const sw_voice *v) {
 if(v->player==SW_DRUMS)return SW_PROFILE_DRUMS;
 if(v->type==SW_STEEL)return SW_PROFILE_STEEL;
 if(v->type==SW_FIDDLE)return SW_PROFILE_FIDDLE;
 if(v->type==SW_GUITAR)return SW_PROFILE_GUITAR;
 if(v->type==SW_PIANO)return SW_PROFILE_PIANO;
 if(v->type==SW_BASS)return SW_PROFILE_BASS;
 return SW_PROFILE_SYNTH;
}
#define SW_FORCE_SYNTH(e) ((e)->profile.force_synth!=0)
#else
#define SW_FORCE_SYNTH(e) 0
#endif
#if defined(SW_TARGET_RAM_AUDIO)&&!defined(__APPLE__)
#define SW_RAM_BRIDGE __attribute__((section(".text.ram_bridge"),noinline))
SW_RAM_BRIDGE static uint32_t score_render_bridge(sw_engine *e) {
 uint32_t (*volatile target)(sw_engine *)=score_render_tick;return target(e);
}
SW_RAM_BRIDGE static void lead_dispatch_bridge(sw_engine *e,uint32_t tick) {
 void (*volatile target)(sw_engine *,uint32_t)=lead_dispatch;target(e,tick);
}
SW_RAM_BRIDGE static void dispatch_bridge(sw_engine *e) {
 void (*volatile target)(sw_engine *)=dispatch;target(e);
}
SW_RAM_BRIDGE static uint32_t classic_render_bridge(sw_engine *e) {
 uint32_t (*volatile target)(sw_engine *)=classic_render_tick;return target(e);
}
SW_RAM_BRIDGE static void dance_step_bridge(sw_engine *e) {
 void (*volatile target)(sw_engine *)=dance_step;target(e);
}
#define SW_SCORE_RENDER(e) score_render_bridge(e)
#define SW_LEAD_DISPATCH(e,t) lead_dispatch_bridge(e,t)
#define SW_DISPATCH(e) dispatch_bridge(e)
#define SW_CLASSIC_RENDER(e) classic_render_bridge(e)
#define SW_DANCE_STEP(e) dance_step_bridge(e)
#else
#define SW_SCORE_RENDER(e) score_render_tick(e)
#define SW_LEAD_DISPATCH(e,t) lead_dispatch(e,t)
#define SW_DISPATCH(e) dispatch(e)
#define SW_CLASSIC_RENDER(e) classic_render_tick(e)
#define SW_DANCE_STEP(e) dance_step(e)
#endif
#if defined(SW_TARGET_RAM_AUDIO)&&!defined(__APPLE__)
#define random32 random32_audio
#define saturate saturate_audio
#define osc osc_audio
#define sw_profile_now sw_profile_now_audio
#define sw_profile_split sw_profile_split_audio
#define SW_TIMBRE_STATUS() sw_timbre_bank_status_audio()
#else
#define SW_TIMBRE_STATUS() sw_timbre_bank_status()
#endif
SW_RAM_AUDIO void sw_render(sw_engine *e,float *stereo,uint32_t frames) {
 uint32_t f,i,classic_wait=0,dance_wait=0,score_wait=0;
 const float steel_brightness=.34f+e->glow*.30f;
 const float steel_attack=160.f+e->swell*5200.f;
 #ifdef SW_WITH_TIMBRE_BANK
 const int timbre_ready=SW_TIMBRE_STATUS()==SW_TB_READY&&!SW_FORCE_SYNTH(e);
 #endif
 for(f=0;f<frames;f++) {
  #ifdef SW_RENDER_PROFILE
  uint32_t profile_mark=sw_profile_now(&e->profile),live_voices=0;
  #endif
  float left=0,right=0,drum_left=0,drum_right=0,call_level=0;
  if(e->score_playing) {
   /* The score clock is also observed by the band at dispatch and by UI
      readers after sw_render. Refresh it at both boundaries. */
   if(score_wait&&e->tick_remaining&&f+1<frames)score_wait--;
   else score_wait=SW_SCORE_RENDER(e);
  }
  if(e->playing&&e->lead_second&&e->lead_wait&&!--e->lead_wait) {e->lead_second=0;SW_LEAD_DISPATCH(e,(e->tick-1)*2+1);}
  e->modern+=.001f*(e->modern_target-e->modern);
  if(e->modern_target==0&&e->modern<.000001f)e->modern=0;
  if(e->modern_target==1.f&&e->modern>.99995f)e->modern=1.f;
  e->hiphop+=.001f*(e->hiphop_target-e->hiphop);
  /* Settle at every requested value: float rounding otherwise leaves 50%
     just below the electro-bass handoff, indefinitely. */
  float hiphop_gap=e->hiphop_target-e->hiphop;
  if(hiphop_gap>-.00005f&&hiphop_gap<.00005f)e->hiphop=e->hiphop_target;
  if(e->playing&&!e->tick_remaining){SW_DISPATCH(e);classic_wait=dance_wait=score_wait=0;}
  #ifdef SW_RENDER_PROFILE
  uint32_t classic_mark=sw_profile_now(&e->profile);
  #endif
  if(classic_wait)classic_wait--;else if(e->playing&&e->song.classic_id)classic_wait=SW_CLASSIC_RENDER(e);
  #ifdef SW_RENDER_PROFILE
  sw_profile_split(&e->profile,classic_mark,&e->profile.ticks[SW_PROFILE_CLASSIC]);
  #endif
  if(e->playing&&e->tick_remaining)e->tick_remaining--;
  if(e->playing) {
   /* The quarter clock still advances on every frame. The next band tick
      resets the wait, including a swing split or an arrangement change. */
   if(dance_wait)dance_wait--;
   else {
    uint32_t sub=e->dance_sub;
    if(sub>=4)dance_wait=frames-f-1u;
    else {
     uint32_t at=sub<2?e->dance_split*sub/2:e->dance_split+(e->dance_length-e->dance_split)*(sub-2)/2;
     if(e->dance_clock>=at)SW_DANCE_STEP(e);
     else dance_wait=at-e->dance_clock-1u;
    }
   }
   e->dance_clock++;
  }
  #ifdef SW_RENDER_PROFILE
  profile_mark=sw_profile_split(&e->profile,profile_mark,&e->profile.ticks[SW_PROFILE_CONTROL]);
  #endif
  for(i=0;i<SW_ACTIVE_VOICES;i++) {
   sw_voice *v=&e->voice[i];float s=0,a,noise;
   if(!v->active)continue;
   #ifdef SW_RENDER_PROFILE
   live_voices++;
   #endif
   if(v->onset_delay) {v->onset_delay--;continue;}
   /* Retire only released tails below the weighted-amplitude floor. This
      prevents nearly silent guitar/arp tails consuming slots needed by the
      next written chord, without shortening any held source note. */
   if(++v->age>=v->life||v->envelope<.0002f||(v->age>v->gate&&v->envelope*v->amplitude<(e->hiphop>0?.0005f:.00003f))) {v->active=0;continue;}
   /* Retire very quiet percussion during the crossfade too. Held musical
      source gates remain unchanged; HIP-HOP zero keeps its exact renderer. */
   if(e->hiphop>0&&v->age>2205u&&((v->type>=SW_KICK&&v->type<=SW_OPEN_HAT)||v->type==SW_BELL)&&v->envelope*v->amplitude<.0003f) {v->active=0;continue;}
   if(v->age>v->gate)v->envelope*=v->type==SW_STEEL?.9992f:v->type==SW_FIDDLE&&v->sample_bank?.99f:v->type==SW_FIDDLE&&!v->string_half?.99945f:.9988f;
   v->envelope*=v->decay;
   /* Fade the quiet Now tail, but keep the voice slot and its original
      envelope until normal retirement. Slot order determines how the
      other instruments share their noise generator. */
   if(v->type==SW_STEEL&&v->age>v->gate) {
    if(v->body2>=1.f)continue;
    if(v->body2>0.f||(e->modern>.0001f&&v->envelope*v->amplitude<.01f))
     v->body2+=1.f/256.f;
   }
   if(v->bending&&v->age<SW_SLIDE_FRAMES)v->increment=(uint32_t)((float)v->increment+v->bend_step);
   else if(v->bending) {v->increment=v->target;v->bending=0;}
   v->phase+=v->increment;
   #ifdef SW_RENDER_PROFILE
   uint32_t voice_type_mark=sw_profile_now(&e->profile),voice_scope=profile_voice_scope(v);
   #endif
   switch(v->type) {
   case SW_STEEL: {
    float x=osc(v->phase);
    float pluck=string_sample(e,i,v);
    /* Bright picked non-pedal steel, with a clean pickup/amp rolloff. No
       permanently running detuned sine pair underneath the string. */
    v->filter+=steel_brightness*(pluck-v->filter);
    v->body1+=.018f*(v->filter-v->body1);
    s=1.65f*(v->filter-.20f*v->body1)+.035f*x/(1.f+(float)v->age*.0005f);
    if(e->modern>0)s=s*(1.f-.3f*e->modern)+(.6f*x+.35f*saw(v->phase,v->increment))*.3f*e->modern;
    if((float)v->age<steel_attack)s*=(float)v->age/steel_attack;
    if(v->body2>0.f)s*=1.f-v->body2;
    break; }
   case SW_FIDDLE: {
    v->phase2+=v->increment+v->increment/1000;
    #ifdef SW_WITH_TIMBRE_BANK
    if(timbre_ready) {
    #endif
    #ifdef SW_RENDER_PROFILE
    uint32_t pcm_mark=sw_profile_now(&e->profile);
    #endif
    const uint8_t *sample=sw_fiddle_pcm[v->string_position];
    uint32_t length=SW_FIDDLE_LENGTH;
    uint32_t at=(uint32_t)v->sample_position,next=at+1;
    if(at>=length)at=next=length-1;
    if(next>=length)next=sw_fiddle_loop[v->string_position];
    float fraction=v->sample_position-(float)at;
    float first=(float)decode_table[sample[at]],second=(float)decode_table[sample[next]];
    s=(first+(second-first)*fraction)*(1.f/32768.f)*1.35f;
    if(v->string_half) {
     float short_bow=0;
     /* At these gates the voice releases before the first sustain wrap.
        Use the same pitched source clock for the two matching roots. */
     if(v->sample_position<SW_FIDDLE_SHORT_LENGTH) {
      uint32_t end=at+1;if(end>=SW_FIDDLE_SHORT_LENGTH)end=at;
      const uint8_t *stroke=sw_fiddle_short_pcm[v->string_position*2u+v->string_half-1u];
      float a0=decode_table[stroke[at]],a1=decode_table[stroke[end]];
      short_bow=(a0+(a1-a0)*fraction)*(1.35f/32768.f);
     }
     s=s*(1.f-v->string_output)+short_bow*v->string_output;
    }
    v->sample_position+=(float)v->increment*v->sample_scale;
    if(v->sample_position>=SW_FIDDLE_LENGTH)v->sample_position-=SW_FIDDLE_LENGTH-sw_fiddle_loop[v->string_position];
    a=(float)v->age/v->string_period;if(a<1)s*=a*(2.f-a);
    #ifdef SW_RENDER_PROFILE
    sw_profile_split(&e->profile,pcm_mark,&e->profile.ticks[SW_PROFILE_PCM]);
    #endif
    #ifdef SW_WITH_TIMBRE_BANK
    }
    #endif
    /* Analogue violin colour at Now: single saw, gentle HP/LP and delayed
       vibrato. The written pitch/octave stays unchanged. */
    float colour=(float)e->fiddle_colour*.01f;
    #ifdef SW_WITH_TIMBRE_BANK
    if(!timbre_ready) {colour=1.f;v->string_slide=1.f;}
    #endif
    if(colour<.85f*e->modern)colour=.85f*e->modern;
    v->string_slide+=.001f*(colour-v->string_slide);
    if(v->string_slide>.000001f) {
     /* Independent oscillator: vibrato cannot retune acoustic playback. */
     v->phase2-=v->increment/1000u;
     /* Skip the oscillator read and arithmetic until vibrato depth is nonzero. */
     if(v->age>6615u) {
      float depth=(float)(v->age-6615u)/6615.f;if(depth>1)depth=1;
      v->phase2+=(uint32_t)(int32_t)((float)v->increment*.0046f*depth*osc(v->age*535654u));
     }
     float synth=saw(v->phase2,v->increment);v->body1+=v->string_previous*(synth-v->body1);
     /* Two gentle low-pass poles round the raw saw's high harmonics. A
        bounded amount of feedback preserves a focused bowed midrange. */
     float bowed=synth-v->body1+.45f*(v->filter-v->body2);
     v->filter+=v->string_target*(bowed-v->filter);
     v->body2+=v->string_target*(v->filter-v->body2);
     float attack=(float)v->age/(v->gate<7938?v->gate/3.f:2646.f);if(attack>1)attack=1;
     s=s*(1.f-v->string_slide)+.95f*v->body2*v->string_slide*attack*(2.f-attack);
    }
    if(v->sample_bank) {
     /* A restrained parallel low-pass distinguishes the second instrument
        at every acoustic/analogue blend. Its state reuses the unbent
        partner's otherwise idle bend_step: no new RAM, sample or voice. */
     v->bend_step+=.38f*(s-v->bend_step);
     s=.65f*s+.35f*v->bend_step;
    }
    break; }
   case SW_BASS: {
    /* Preserve noise consumption so the other players' excitation is stable. */
    if(v->age<1200)(void)random32(&e->noise);
    if(v->sample_position>=SW_UPRIGHT_LENGTH||(!v->manual_voice&&e->modern==1.f&&v->age>=1200)) {v->active=0;break;}
    #ifdef SW_WITH_TIMBRE_BANK
    if(!timbre_ready) {float x=osc(v->phase);s=(.75f*x+.12f*osc(v->phase*2u))/(1.f+(float)v->age*.00012f);break;}
    #endif
    if(v->sample_position<SW_UPRIGHT_LENGTH) {
     #ifdef SW_RENDER_PROFILE
     uint32_t pcm_mark=sw_profile_now(&e->profile);
     #endif
     uint32_t at=(uint32_t)v->sample_position,next=at+1;if(next>=SW_UPRIGHT_LENGTH)next=at;
     const uint8_t *sample=sw_upright_pcm[(uint32_t)v->body2];
     float first=decode_table[sample[at]],second=decode_table[sample[next]];
     s=(first+(second-first)*(v->sample_position-at))*(2.f/32768.f);
     v->sample_position+=(float)v->increment*v->sample_scale;
     #ifdef SW_RENDER_PROFILE
     sw_profile_split(&e->profile,pcm_mark,&e->profile.ticks[SW_PROFILE_PCM]);
     #endif
    }
    break; }
   case SW_GUITAR: {
    float pluck=string_sample(e,i,v),acoustic=0;
    if(!SW_FORCE_SYNTH(e)&&v->sample_position<SW_ARCHTOP_LENGTH) {
     #ifdef SW_RENDER_PROFILE
     uint32_t pcm_mark=sw_profile_now(&e->profile);
     #endif
     if(e->modern!=1.f) {
      uint32_t at=(uint32_t)v->sample_position,next=at+1;if(next>=SW_ARCHTOP_LENGTH)next=at;
      const uint8_t *sample=period_archtop[v->sample_bank];
      float a0=decode_table[sample[at]],a1=decode_table[sample[next]];
      acoustic=(a0+(a1-a0)*(v->sample_position-at))*(1.05f/32768.f);
     }
     v->sample_position+=(float)v->increment*v->sample_scale;
     #ifdef SW_RENDER_PROFILE
     sw_profile_split(&e->profile,pcm_mark,&e->profile.ticks[SW_PROFILE_PCM]);
     #endif
    }
    s=SW_FORCE_SYNTH(e)?.85f*pluck:acoustic;
    if(e->modern>0) {float x=osc(v->phase),amp=saturate((pluck+.15f*x)*(2.f+8.f*e->modern));v->filter+=.18f*(amp-v->filter);s=s*(1.f-e->modern)+(.85f*v->filter+.15f*amp)*e->modern;}
    break; }
   case SW_PIANO: {
    /* Short upright chord strokes: recorded hammer/body decay, no looping
       sine sustain. Keep noise RNG consumption unchanged for the other band
       voices so a piano timbre edit cannot alter their excitation. */
    if(v->age<1000)(void)random32(&e->noise);
    #ifdef SW_WITH_TIMBRE_BANK
    if(!timbre_ready) {float x=osc(v->phase);s=(.65f*x+.22f*osc(v->phase*2u)+.1f*osc(v->phase*3u))/(1.f+(float)v->age*.0013f);break;}
    #endif
    if(v->sample_position>=SW_PIANO_LENGTH) {v->active=0;break;}
    #ifdef SW_RENDER_PROFILE
    uint32_t pcm_mark=sw_profile_now(&e->profile);
    #endif
    uint32_t at=(uint32_t)v->sample_position,next=at+1;
    if(next>=SW_PIANO_LENGTH)next=at;
    const uint8_t *sample=sw_piano_pcm[v->string_position];
    float first=(float)decode_table[sample[at]],second=(float)decode_table[sample[next]];
    s=(first+(second-first)*(v->sample_position-(float)at))*(2.f/32768.f);
    v->sample_position+=(float)v->increment*v->sample_scale;
    #ifdef SW_RENDER_PROFILE
    sw_profile_split(&e->profile,pcm_mark,&e->profile.ticks[SW_PROFILE_PCM]);
    #endif
    break; }
   case SW_HORNS: {
    #ifdef SW_WITH_TIMBRE_BANK
    if(!timbre_ready) {float x=saw(v->phase,v->increment);v->filter+=v->string_target*(x-v->filter);s=v->filter;break;}
    #endif
    #ifdef SW_RENDER_PROFILE
    uint32_t pcm_mark=sw_profile_now(&e->profile);
    #endif
    uint32_t at=(uint32_t)v->sample_position,next=at+1;
    if(next>=SW_REED_LENGTH)next=sw_reed_loop[v->string_position];
    const uint8_t *sample=sw_reed_pcm[v->string_position];
    float first=decode_table[sample[at]],second=decode_table[sample[next]];
    float reed=(first+(second-first)*(v->sample_position-at))*(1.7f/32768.f);
    v->sample_position+=(float)v->increment*v->sample_scale;
    if(v->sample_position>=SW_REED_LENGTH)v->sample_position-=SW_REED_LENGTH-sw_reed_loop[v->string_position];
    #ifdef SW_RENDER_PROFILE
    sw_profile_split(&e->profile,pcm_mark,&e->profile.ticks[SW_PROFILE_PCM]);
    #endif
    v->filter+=v->string_target*(saw(v->phase,v->increment)-v->filter);
    if(e->modern>0) {float x=osc(v->phase);s=reed*(1.f-e->modern)+(.95f*v->filter+.18f*x)*e->modern;}
    else s=reed;
    if(v->age<900)s*=(float)v->age/900.f;break; }
   case SW_SYNTH: {float x=osc(v->phase);s=.7f*x+.2f*osc(v->phase*3)+.1f*osc(v->phase*5);break;}
   case SW_PAD: {
    uint32_t detuned=v->increment+v->increment/1000;
    v->phase2+=detuned;
    s=.5f*saw(v->phase,v->increment)+.5f*saw(v->phase2,detuned);v->filter+=.045f*(s-v->filter);s=v->filter;
    if(v->age<3500)s*=(float)v->age/3500.f;break; }
   case SW_ARP: {
    float x=osc(v->phase);s=.65f*saw(v->phase,v->increment)+.35f*x;v->filter+=(.06f+.18f*v->envelope)*(s-v->filter);s=v->filter;break; }
   case SW_KICK: {
    float x=osc(v->phase);
    s=1.05f*x+e->modern*.20f*osc(v->phase*2);if(v->age<1600)v->increment-=v->increment/1900u;
    if(v->age<180) {noise=(float)(int32_t)random32(&e->noise)*(1.f/2147483648.f);s+=noise*.16f*(1.f-(float)v->age/180.f);}
    float acoustic=0;
    if(!SW_FORCE_SYNTH(e)&&v->sample_position<SW_ACOUSTIC_KICK_LENGTH) {
     #ifdef SW_RENDER_PROFILE
     uint32_t pcm_mark=sw_profile_now(&e->profile);
     #endif
     if(e->modern!=1.f) {
      uint32_t at=(uint32_t)v->sample_position,next=at+1;if(next>=SW_ACOUSTIC_KICK_LENGTH)next=at;
      const uint8_t *sample=period_kick[v->sample_bank];
      float a0=decode_table[sample[at]],a1=decode_table[sample[next]];
      acoustic=(a0+(a1-a0)*(v->sample_position-at))*(1.2f/32768.f);
     }
     v->sample_position+=(float)SW_ACOUSTIC_KICK_RATE/SW_RATE;
     #ifdef SW_RENDER_PROFILE
     sw_profile_split(&e->profile,pcm_mark,&e->profile.ticks[SW_PROFILE_PCM]);
     #endif
    }
    if(!SW_FORCE_SYNTH(e))s=acoustic*(1.f-e->modern)+s*e->modern;break; }
   case SW_BELL: {
    /* 808-style cowbell: two square waves about a fifth apart through a band-pass */
    float a1=v->phase<0x80000000u?1.f:-1.f,a2=(uint32_t)(v->phase+(v->phase>>1))<0x80000000u?1.f:-1.f;
    float x2=.5f*(a1+a2);v->filter+=.35f*(x2-v->filter);v->body1+=.05f*(v->filter-v->body1);s=.9f*(v->filter-v->body1);break; }
   case SW_CLAP:
    noise=(float)(int32_t)random32(&e->noise)*(1.f/2147483648.f);v->filter+=.12f*(noise-v->filter);
    s=(noise-v->filter)*((v->age<1200&&(v->age%400)<240)?1.f:.4f);break;
   case SW_OPEN_HAT:
    noise=(float)(int32_t)random32(&e->noise)*(1.f/2147483648.f);v->filter+=.08f*(noise-v->filter);s=.55f*(noise-v->filter);break;
   default:
    noise=(float)(int32_t)random32(&e->noise)*(1.f/2147483648.f);
    v->filter+=.23f*(noise-v->filter);s=noise-v->filter;
    if(v->type==SW_SNARE) {
     float x=osc(v->phase);s=.56f*(noise-v->filter)+.32f*v->filter+.24f*x/(1.f+(float)v->age*.001f);
     s+=e->modern*.18f*(osc(v->phase*3)+osc(v->phase*5));
     float tap=0;
     if(!SW_FORCE_SYNTH(e)&&v->sample_position<SW_SNARE_LENGTH) {
      #ifdef SW_RENDER_PROFILE
      uint32_t pcm_mark=sw_profile_now(&e->profile);
      #endif
      if(e->modern!=1.f) {
       uint32_t at=(uint32_t)v->sample_position,next=at+1;if(next>=SW_SNARE_LENGTH)next=at;
       const uint8_t *sample=period_snare[v->string_position];float first=decode_table[sample[at]],second=decode_table[sample[next]];
       tap=(first+(second-first)*(v->sample_position-at))*(2.f/32768.f);
      }
      v->sample_position+=(float)SW_SNARE_RATE/SW_RATE;
      #ifdef SW_RENDER_PROFILE
      sw_profile_split(&e->profile,pcm_mark,&e->profile.ticks[SW_PROFILE_PCM]);
      #endif
     }
     if(!SW_FORCE_SYNTH(e))s=tap*(1.f-e->modern)+s*e->modern;
    }
    if(v->type==SW_HAT) {
     float tap=0;
     if(!SW_FORCE_SYNTH(e)&&v->sample_position<SW_ACOUSTIC_HAT_LENGTH) {
      #ifdef SW_RENDER_PROFILE
      uint32_t pcm_mark=sw_profile_now(&e->profile);
      #endif
      if(e->modern!=1.f) {
       uint32_t at=(uint32_t)v->sample_position,next=at+1;if(next>=SW_ACOUSTIC_HAT_LENGTH)next=at;
       const uint8_t *sample=period_hat[v->sample_bank];
       float a0=decode_table[sample[at]],a1=decode_table[sample[next]];
       tap=(a0+(a1-a0)*(v->sample_position-at))*(1.2f/32768.f);
      }
      v->sample_position+=(float)SW_ACOUSTIC_HAT_RATE/SW_RATE;
      #ifdef SW_RENDER_PROFILE
      sw_profile_split(&e->profile,pcm_mark,&e->profile.ticks[SW_PROFILE_PCM]);
      #endif
     }
     if(!SW_FORCE_SYNTH(e))s=tap*(1.f-e->modern)+s*e->modern;
    }
    break;
   }
   if(v->age<80)s*=(float)v->age/80.f;
   s*=v->amplitude*v->envelope;
   if(v->type==SW_BASS&&!v->manual_voice)s*=1.f-e->modern;
   if(v->type==SW_PAD||v->type==SW_ARP)s*=e->modern;
   if(v->player==SW_DRUMS&&e->modern>0) {drum_left+=s*v->pan_left;drum_right+=s*v->pan_right;}
   else {left+=s*v->pan_left;right+=s*v->pan_right;}
   #ifdef SW_RENDER_PROFILE
   sw_profile_split(&e->profile,voice_type_mark,&e->profile.ticks[voice_scope]);
   #endif
  }
  #ifdef SW_RENDER_PROFILE
  profile_mark=sw_profile_split(&e->profile,profile_mark,&e->profile.ticks[SW_PROFILE_VOICES]);
  if(live_voices>e->profile.peak_voices)e->profile.peak_voices=live_voices;
  #endif
  #ifdef SW_RENDER_PROFILE
  uint32_t mix_mark=profile_mark,stage_mark=profile_mark;
  #endif
  {float acid=acid_sample(e);left+=acid*.5f;right+=acid*.5f;}
  #ifdef SW_RENDER_PROFILE
  stage_mark=sw_profile_split(&e->profile,stage_mark,&e->profile.ticks[SW_PROFILE_ACID]);
  #endif
  {float record=record_sample(e);left+=record*.55f;right+=record*.45f;}
  #ifdef SW_WITH_BANK
  {float record=sw_sample_read(&e->sample_player);left+=record*.55f;right+=record*.45f;}
  #endif
  #ifdef SW_RENDER_PROFILE
  stage_mark=sw_profile_split(&e->profile,stage_mark,&e->profile.ticks[SW_PROFILE_RECORD]);
  #endif
  {float duck=1.f-e->modern*e->pump*.46f;e->pump*=.99955f;left=left*duck+drum_left;right=right*duck+drum_right;}
  {uint32_t p=e->delay_pos;float echo=e->delay[p];
   e->delay[p]=(left+right)*.28f+echo*.26f;e->delay_pos=(p+1)&4095;
   left+=echo*e->air*.45f;right+=e->delay[(p+1137)&4095]*e->air*.45f;}
  #ifdef SW_RENDER_PROFILE
  stage_mark=sw_profile_split(&e->profile,stage_mark,&e->profile.ticks[SW_PROFILE_ECHO]);
  #endif
  #ifdef SW_WITH_CUE
  if(e->cue_kind<sw_call_count()&&e->cue_position<(call_frames_audio(e->cue_kind)<<12)) {
   float cue=call_sample(e);
   call_level=(cue<0?-cue:cue)/.24f;
   left=left*.58f+cue;right=right*.58f+cue;
  }
  #endif
  #ifdef SW_RENDER_PROFILE
  sw_profile_split(&e->profile,stage_mark,&e->profile.ticks[SW_PROFILE_CUE]);
  #endif
  e->call_envelope+=.001f*(call_level-e->call_envelope);
  e->master+=.001f*(e->master_target-e->master);
  left*=e->master*2.4f;right*=e->master*2.4f;
  /* Bounded soft saturation; no hidden normalization or automatic gain. */
  left=left/(1.f+(left<0?-left:left));right=right/(1.f+(right<0?-right:right));
  stereo[f*2]=left;stereo[f*2+1]=right;e->sample++;e->rendered_frames++;
  if(e->walking&&!e->title&&!(e->cue_kind<sw_call_count()&&e->cue_position<(call_frames_audio(e->cue_kind)<<12))&&++e->walk_clock>=SW_RATE*8)e->walking=0;
  #ifdef SW_RENDER_PROFILE
  sw_profile_split(&e->profile,mix_mark,&e->profile.ticks[SW_PROFILE_MIX]);e->profile.frames++;
  #endif
 }
}
#if defined(SW_TARGET_RAM_AUDIO)&&!defined(__APPLE__)
#undef random32
#undef saturate
#undef osc
#undef sw_profile_now
#undef sw_profile_split
#endif
#undef SW_TIMBRE_STATUS
#undef SW_FORCE_SYNTH

#ifdef SW_SOUND_PROBE
/* Diagnostic-only entry point; omitted from host, WASM and firmware. */
void sw_sound_probe(sw_engine *e,uint8_t type,uint8_t pitch,uint32_t gate) {note(e,type, type==SW_DRUMS?SW_SNARE:type,pitch,.4f,gate,0,0);}
void sw_drum_probe(sw_engine *e,uint8_t type) {note(e,SW_DRUMS,type,38,.4f,6000,0,0);}
#endif
