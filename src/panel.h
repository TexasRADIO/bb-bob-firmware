/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef SW_PANEL_H
#define SW_PANEL_H
#include "swing.h"
enum { SWP_HOME,SWP_BAND,SWP_TONE,SWP_GROOVE,SWP_BOB,SWP_SONG,SWP_KEEPS,SWP_DIRECT };
/* Reuse the former duplicate Sound page; existing page IDs remain stable. */
enum { SWP_STYLE=SWP_GROOVE };
enum { SWP_OCT_DOWN,SWP_OCT_UP,SWP_FX,SWP_SEL,SWP_ENV,SWP_LFO,SWP_EDIT,SWP_GLO,SWP_HOME_BUTTON,SWP_SAVE,SWP_ARP,SWP_SEQ,SWP_PLAY,SWP_REC };
enum { SWP_Q_NONE,SWP_Q_SONG,SWP_Q_KEY,SWP_Q_FORM,SWP_Q_END,SWP_Q_THEME,SWP_Q_LEON,SWP_APPLIED=128 };
#define SWP_IDLE_MS 8000u
#define SWP_CUE_MS 2500u
#define SWP_DOUBLE_MS 400u
typedef struct {
 uint32_t seed;
 uint16_t bpm;
 uint8_t valid,classic_id,key,form,players,swing,modern,hiphop,air,glow,swell,fiddle,texture,auto_bob,source_solos;
} sw_keep;
typedef struct {
 sw_keep keeps[8];
 uint32_t seed,held_keys;
 uint16_t notice_ms,buttons;
 uint8_t page,voice,bank,slot,request,oct_pair;
 int8_t octave;
 const char *notice;
 uint32_t queued_rev;
 uint16_t idle_ms,status_ms,fill_ms,encore_ms;
 uint8_t was_playing;
 uint8_t cursor,queued_kind,queued_value,family,last_master;
 /* Refreshed only in foreground: pad presses/LEDs never scan call metadata
    inside the target's audio interrupt mask. */
 uint16_t call_slots[24],family_counts[5],call_count;
 uint8_t mapped_family,mapped_bank,cue_touch;
} sw_panel;
void sw_panel_init(sw_panel *p);
void sw_panel_button(sw_engine *e,sw_panel *p,uint32_t button);
void sw_panel_edge(sw_engine *e,sw_panel *p,uint32_t button,uint32_t down);
/* HAL edge accumulators may contain both edges of a tap between main-loop
 * polls. Preserve that tap and end it; aggregate octave downs before ups. */
void sw_panel_buttons_poll(sw_engine *e,sw_panel *p,uint32_t pressed,uint32_t released,uint32_t levels);
void sw_panel_keys_poll(sw_engine *e,sw_panel *p,uint32_t levels,uint32_t pressed,uint32_t previous);
void sw_panel_encoder(sw_engine *e,sw_panel *p,uint32_t encoder,int32_t steps);
void sw_panel_key(sw_engine *e,sw_panel *p,uint32_t key,uint32_t down);
void sw_panel_release(sw_engine *e,sw_panel *p);
/* Planner work belongs in main context, outside the audio interrupt lock. */
void sw_panel_service(sw_engine *e,sw_panel *p);
void sw_panel_tick(sw_panel *p,uint32_t milliseconds);
void sw_panel_touch(sw_panel *p);
void sw_panel_refresh_calls(sw_panel *p);
/* Foreground display/control observation, not an audio-ISR hook. Target calls
 * this under its existing bounded control lock; drawing remains read-only. */
void sw_panel_observe(sw_engine *e,sw_panel *p);
/* ADC sampling is not control activity. Shared by the hardware and host paths;
 * the observer handles real movement and ignores one-percent ADC chatter. */
void sw_panel_master_poll(sw_engine *e,int32_t level);
uint32_t sw_panel_call_slots(const sw_panel *p,uint16_t slots[24]);
const char *sw_panel_family_name(uint32_t family);
const char *sw_panel_knob_label(uint32_t page,uint32_t knob);
uint32_t sw_panel_knob_value(const sw_engine *e,const sw_panel *p,uint32_t knob);
const char *sw_panel_status_label(const sw_engine *e,const sw_panel *p);
uint32_t sw_panel_level_notice(const sw_panel *p);
uint32_t sw_panel_then_now_notice(const sw_panel *p);
uint32_t sw_panel_led(const sw_engine *e,const sw_panel *p,uint32_t id);
const char *sw_panel_page_name(uint32_t page);
uint32_t sw_panel_white(uint32_t index);
uint32_t sw_panel_player(const sw_panel *p);
#endif
