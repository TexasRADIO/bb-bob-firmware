/* SPDX-License-Identifier: GPL-3.0-only; original shared physical-panel director. */
#include "panel.h"
static int bounded(int x,int lo,int hi) {return x<lo?lo:x>hi?hi:x;}
static int wrap(int x,int n) {x%=n;return x<0?x+n:x;}
static void say(sw_panel *p,const char *s) {p->notice=s;p->notice_ms=1800;}
/* Bob reacts to the player. Visual state only (the gesture clock), never the audio path.
   0 look, 1 cigar puff, 2 ash, 3/4 arm gestures, 5 laugh. */
static void react(sw_engine *e,uint32_t gesture) {if(sw_call_active(e))return;e->gesture=(uint8_t)gesture;e->gesture_time=0;e->gesture_active=1;}
static const char then_now_notice[]="THEN/NOW";
uint32_t sw_panel_white(uint32_t i) {static const uint8_t keys[8]={0,2,4,6,7,9,11,12};return i<8?keys[i]:27;}
uint32_t sw_panel_player(const sw_panel *p) {static const uint8_t voices[7]={SW_STEEL,SW_FIDDLE,SW_GUITAR,SW_PIANO,SW_BASS,SW_HORNS,SW_SYNTH};return voices[p->voice%7u];}
const char *sw_panel_page_name(uint32_t page) {static const char *const names[]={"PLAY KEYS","BAND","SOUND","BAND","BOB","SONGS","KEEPS","DANCE HALL"};return page<8?names[page]:"PLAY KEYS";}
void sw_panel_init(sw_panel *p) {
 uint8_t *b=(uint8_t *)p;for(uint32_t i=0;i<sizeof *p;i++)b[i]=0;p->page=SWP_DIRECT;p->seed=301026;p->last_master=65;p->mapped_family=255;sw_panel_refresh_calls(p);
}
void sw_panel_release(sw_engine *e,sw_panel *p) {sw_keys_release(e);p->held_keys=0;}
static void page(sw_engine *e,sw_panel *p,uint32_t next) {
 if(p->page==next&&!e->title)return;
 uint32_t was_keys=p->page==SWP_HOME||p->page==SWP_TONE||p->page==SWP_GROOVE||p->page==SWP_SONG;
 uint32_t stays_keys=next==SWP_HOME||next==SWP_TONE||next==SWP_GROOVE||next==SWP_SONG;
 if(!was_keys||!stays_keys)sw_panel_release(e,p);
 p->page=(uint8_t)next;e->title=0;sw_command(e,SW_PAD_MODE,next==SWP_BOB);
}
static void fresh(sw_engine *e,sw_panel *p) {
 e->pending_ready=0;e->classic_request=0;p->seed=p->seed*1664525u+1013904223u;p->request=1;say(p,"NEW TUNE - NEXT BAR");
}
static void keep(sw_engine *e,sw_panel *p) {
 if(e->pending_ready||e->classic_request||p->request) {say(p,"WAIT FOR NEXT BAR");return;}
 sw_keep *k=&p->keeps[p->slot];k->valid=1;k->seed=e->song.seed;k->classic_id=e->song.classic_id;k->key=e->song.key;k->form=e->song.form;k->bpm=e->bpm;k->players=e->players;k->swing=(uint8_t)e->swing;k->modern=(uint8_t)(e->modern_target*100.f+.5f);k->hiphop=(uint8_t)(e->hiphop_target*100.f+.5f);k->air=(uint8_t)(e->air*100.f+.5f);k->glow=(uint8_t)(e->glow*100.f+.5f);k->swell=(uint8_t)(e->swell*100.f+.5f);k->fiddle=e->fiddle_colour;k->texture=e->texture;k->auto_bob=e->auto_calls;k->source_solos=e->source_solos;say(p,"KEPT UNTIL POWER OFF");
}
void sw_panel_edge(sw_engine *e,sw_panel *p,uint32_t id,uint32_t down) {
 if(id>=14)return;sw_panel_touch(p);
 uint16_t bit=(uint16_t)(1u<<id);
 if(down) {
  if(p->buttons&bit)return;p->buttons|=bit;
  if(id<2) {if((p->buttons&3u)==3u) {p->oct_pair=1;sw_panel_release(e,p);p->octave=0;say(p,"KEYBOARD RESET");}}
  else sw_panel_button(e,p,id);
 } else {
  if(!(p->buttons&bit))return;p->buttons&=(uint16_t)~bit;
  if(id<2) {if(!p->oct_pair)sw_panel_button(e,p,id);if(!(p->buttons&3u))p->oct_pair=0;}
 }
}
/* Five exclusive presentation families use reviewed bank flags, not inferred
 * speaker identity. PRESETS traverses pages within a family then the next;
 * GLO jumps to the next nonempty family. No clip is lost after pad 24. */
static uint32_t family_for(uint32_t index) {
 uint32_t f=sw_call_metadata_flags(index);
 /* Bank pools: 0 Ah-ha, 1 Yeah, 3 Ah, 4 Alright, 5 band/player cue. */
 return f&9u?0:f&32u?1:f&2u?2:f&16u?3:4;
}
const char *sw_panel_family_name(uint32_t family) {static const char *const names[]={"AH-HA","PLAYERS","YEAH","CHEERS","MORE"};return names[family%5u];}
uint32_t sw_panel_call_slots(const sw_panel *p,uint16_t slots[24]) {
 uint32_t count=0,skip=(uint32_t)p->bank*24;
 for(uint32_t k=0;k<24;k++)slots[k]=0xffff;
 for(uint32_t i=0;i<sw_call_count();i++)if(family_for(i)==p->family%5u) {
  if(count>=skip&&count<skip+24)slots[count-skip]=(uint16_t)i;count++;
 }
 return count;
}
void sw_panel_refresh_calls(sw_panel *p) {
 uint32_t count=sw_call_count();
 if(p->call_count==count&&p->mapped_family==p->family&&p->mapped_bank==p->bank)return;
 if(p->call_count!=count) {for(uint32_t f=0;f<5;f++)p->family_counts[f]=0;
  for(uint32_t i=0;i<count;i++)p->family_counts[family_for(i)]++;p->call_count=(uint16_t)count;}
 sw_panel_call_slots(p,p->call_slots);p->mapped_family=p->family;p->mapped_bank=p->bank;
}
static void family_step(sw_panel *p,int steps,uint32_t jump) {
 const uint16_t *counts=p->family_counts;int total=0,at=0;
 for(uint32_t f=0;f<5;f++) {if(f<p->family)at+=(counts[f]+23)/24;total+=(counts[f]+23)/24;}
 if(!total) {p->family=p->bank=0;return;}
 if(jump) {for(uint32_t i=1;i<=5;i++) {uint32_t f=(p->family+i)%5;if(counts[f]) {p->family=(uint8_t)f;p->bank=0;return;}}}
 at=wrap(at+p->bank+steps,total);
 for(uint32_t f=0;f<5;f++) {int pages=(counts[f]+23)/24;if(at<pages) {p->family=(uint8_t)f;p->bank=(uint8_t)at;break;}at-=pages;}
}
void sw_panel_touch(sw_panel *p) {p->idle_ms=0;}
static void queued(sw_engine *e,sw_panel *p,uint32_t kind,uint32_t value) {
 p->queued_rev=e->song_revision;p->queued_kind=(uint8_t)kind;p->queued_value=(uint8_t)value;p->status_ms=0;p->notice_ms=0;
}
static void songs(sw_engine *e,sw_panel *p) {
 if(p->page!=SWP_SONG) {p->cursor=e->song.classic_id;page(e,p,SWP_SONG);}
}
static void confirm(sw_engine *e,sw_panel *p) {
 if(!p->cursor)fresh(e,p);
 else if(sw_classic_available(p->cursor-1u)) {e->pending_ready=0;e->classic_request=0;p->request=0;
  /* Set mode directly to avoid scheduling the previously selected song. */
  e->classic_mode=1;sw_command(e,SW_CLASSIC_SELECT,p->cursor-1);}
 else return;
 queued(e,p,SWP_Q_SONG,p->cursor);page(e,p,SWP_DIRECT);
}
static void replan(sw_engine *e,sw_panel *p) {
 if(e->classic_mode) {e->pending_ready=0;return;}
 p->seed=e->pending_ready?e->pending.seed:p->request==1?p->seed:e->song.seed;
 e->pending_ready=0;e->classic_request=0;p->request=3;
}
void sw_panel_button(sw_engine *e,sw_panel *p,uint32_t id) {
 if(id>=14)return;sw_panel_touch(p);p->cue_touch=0;
 switch(id) {
 case SWP_OCT_DOWN:case SWP_OCT_UP:
  sw_panel_release(e,p);p->octave=(int8_t)bounded(p->octave+(id==SWP_OCT_UP?1:-1),-1,1);say(p,"KEYBOARD OCTAVE");break;
 case SWP_FX:
  if(p->fill_ms) {sw_command(e,SW_STOPTIME,0);say(p,"BREAK - TWO BARS");p->fill_ms=0;}
  else {sw_command(e,SW_FILL,0);say(p,"FILL IT UP - TAP AGAIN TO BREAK");p->fill_ms=SWP_DOUBLE_MS;react(e,1);}
  e->title=0;p->cue_touch=p->page==SWP_DIRECT||p->page==SWP_HOME;break;
 case SWP_SEL:page(e,p,SWP_BAND);break;
 case SWP_ENV:page(e,p,SWP_TONE);break;
 case SWP_LFO: /* STYLE page retired: LFO snaps the band between Then and Now */
  sw_command(e,SW_MODERN,e->modern_target<.5f?100:0);p->notice=then_now_notice;p->notice_ms=1500;react(e,3);break;
 case SWP_EDIT:songs(e,p);break;
 case SWP_GLO:if(p->page==SWP_BOB)family_step(p,1,1);else page(e,p,SWP_BOB);break;
 case SWP_HOME_BUTTON:page(e,p,SWP_DIRECT);break;
 case SWP_SAVE:if(p->page!=SWP_KEEPS)page(e,p,SWP_KEEPS);else keep(e,p);break;
 case SWP_ARP:sw_command(e,SW_LEON,0);if(e->leon_pending||sw_call_active(e))queued(e,p,SWP_Q_LEON,0);else say(p,"LEON - NEXT TUNE");p->cue_touch=p->page==SWP_DIRECT||p->page==SWP_HOME;break;
 case SWP_SEQ:page(e,p,SWP_HOME);break;
 case SWP_PLAY:
  if(p->page==SWP_SONG)confirm(e,p);
  else if(p->page==SWP_KEEPS) {if(p->keeps[p->slot].valid) {e->pending_ready=0;e->classic_request=0;p->request=2;queued(e,p,SWP_Q_SONG,p->keeps[p->slot].classic_id);page(e,p,SWP_DIRECT);}else say(p,"EMPTY KEEP");}
  else {uint32_t encore=p->encore_ms&&!e->playing;sw_command(e,SW_PLAY,0);if(!e->playing)sw_panel_release(e,p);
   else if(encore) {sw_command(e,SW_HOLLER,0);react(e,5);say(p,"ENCORE!");}p->encore_ms=0;}break;
 case SWP_REC:fresh(e,p);queued(e,p,SWP_Q_SONG,0);page(e,p,SWP_DIRECT);break;
 }
}
void sw_panel_encoder(sw_engine *e,sw_panel *p,uint32_t id,int32_t steps) {
 if(id>=7||!steps)return;sw_panel_touch(p);p->cue_touch=0;steps=bounded(steps,-1000,1000);e->title=0;
 if(id==0) {
  if(p->page==SWP_KEEPS)p->slot=(uint8_t)wrap(p->slot+steps,8);
  else {sw_panel_release(e,p);p->voice=(uint8_t)wrap(p->voice+steps,7);say(p,"LIVE INSTRUMENT");}
 } else if(id==1) {sw_command(e,SW_MODERN,(int)(e->modern_target*100.f+.5f)+steps*5);p->notice=then_now_notice;p->notice_ms=1500;}
 else if(id==6) {
  if(p->page==SWP_BOB)family_step(p,steps,0);
  else if(p->page==SWP_KEEPS)p->slot=(uint8_t)wrap(p->slot+steps,8);
  else if(p->page==SWP_DIRECT&&e->playing) {
   /* one turn = next tune at the next bar; browse-first stays on SONGS */
   int direction=steps>0?1:-1;p->cursor=e->song.classic_id;
   for(uint32_t n=0;n<sw_classic_slots()+1u;n++) {p->cursor=(uint8_t)wrap(p->cursor+direction,(int)sw_classic_slots()+1);if(p->cursor&&sw_classic_available(p->cursor-1u))break;}
   if(p->cursor&&sw_classic_available(p->cursor-1u)) {e->pending_ready=0;e->classic_request=0;p->request=0;e->classic_mode=1;sw_command(e,SW_CLASSIC_SELECT,p->cursor-1);queued(e,p,SWP_Q_SONG,p->cursor);react(e,4);p->cue_touch=1;}
  }
  else {songs(e,p);int direction=steps>0?1:-1;steps%=(int)(sw_classic_ready_count()+1u);
   for(int i=0;i<(steps>0?steps:-steps);i++)for(uint32_t n=0;n<sw_classic_slots()+1u;n++) {p->cursor=(uint8_t)wrap(p->cursor+direction,(int)sw_classic_slots()+1);if(!p->cursor||sw_classic_available(p->cursor-1u))break;}}
 } else if(p->page==SWP_TONE) {
  if(id==2)sw_command(e,SW_GLOW,(int)(e->glow*100.f+.5f)+steps*2);
  if(id==3)sw_command(e,SW_SWELL,(int)(e->swell*100.f+.5f)+steps*2);
  if(id==4)sw_command(e,SW_FIDDLE_COLOUR,e->fiddle_colour+steps*2);
  if(id==5)sw_command(e,SW_AIR,(int)(e->air*100.f+.5f)+steps*2);
 } else {
  if(id==2)sw_command(e,SW_TEMPO,e->bpm+steps);
  if(id==3) {
   if(p->page==SWP_SONG) {sw_command(e,SW_KEY,wrap(e->key_next+steps,12));replan(e,p);queued(e,p,SWP_Q_KEY,e->key_next);}
   else sw_command(e,SW_SWING,e->swing+steps);
  }
  if(id==4) {
   if(p->page==SWP_SONG||p->page==SWP_DIRECT) {sw_command(e,SW_FORM,wrap(e->form_next+steps,3));
    if(!e->classic_mode) {replan(e,p);queued(e,p,SWP_Q_FORM,e->form_next);}else say(p,"FORM FOR NEXT NEW TUNE");}
   else if(p->page==SWP_BOB)sw_command(e,SW_AUTO_CALL,steps>0);
   else if(p->page==SWP_BAND)sw_command(e,SW_HIPHOP,(int)(e->hiphop_target*100.f+.5f)+steps*2);
   else sw_command(e,SW_GLOW,(int)(e->glow*100.f+.5f)+steps*2);
  }
  if(id==5)sw_command(e,SW_AIR,(int)(e->air*100.f+.5f)+steps*2);
 }
}
void sw_panel_key(sw_engine *e,sw_panel *p,uint32_t key,uint32_t down) {
 if(key>=27)return;sw_panel_touch(p);
 uint32_t bit=1u<<key;if(!down) {p->held_keys&=~bit;sw_key_off(e,key);return;}
 if(p->held_keys&bit)return;p->held_keys|=bit;e->title=0;p->cue_touch=0;
 if(p->page==SWP_BOB) {
  if(key==24) {sw_command(e,SW_LEON,0);if(e->leon_pending||sw_call_active(e))queued(e,p,SWP_Q_LEON,0);}
  else if(key==25)sw_command(e,SW_HOLLER,0);else if(key==26)sw_command(e,SW_RANDOM_CALL,0);
  else if(p->mapped_family==p->family&&p->mapped_bank==p->bank&&p->call_slots[key]!=0xffff)sw_command(e,SW_CALL_PAD,p->call_slots[key]);
 } else if(p->page==SWP_BAND||p->page==SWP_DIRECT||p->page==SWP_KEEPS) {
  for(uint32_t i=0;i<8;i++)if(key==sw_panel_white(i)) {
   if(p->page==SWP_BAND)sw_command(e,SW_PLAYER,(int)i);
   else if(p->page==SWP_KEEPS)p->slot=(uint8_t)i;
   else {static const uint8_t actions[]={SW_FILL,SW_STOPTIME,SW_LEON,SW_HOME,SW_END,SW_BAND,SW_AUTO_CALL,SW_SOURCE_SOLOS};
    int value=i==5?255:i==6?!e->auto_calls:i==7?!e->source_solos:0;sw_command(e,actions[i],value);
    if(i==4) {queued(e,p,SWP_Q_END,0);react(e,5);}else if(i==3) {queued(e,p,SWP_Q_THEME,0);react(e,4);}else if(i==2&&(e->leon_pending||sw_call_active(e)))queued(e,p,SWP_Q_LEON,0);else say(p,"BAND CUE");p->cue_touch=i!=6&&i!=7;}
   return;
  }
  if(p->page==SWP_DIRECT)sw_key_on(e,key,(uint32_t)(53+(int)key+p->octave*12),sw_panel_player(p));
 } else sw_key_on(e,key,(uint32_t)(53+(int)key+p->octave*12),sw_panel_player(p));
}
void sw_panel_service(sw_engine *e,sw_panel *p) {
 sw_panel_refresh_calls(p);
 if(!p->request||e->pending_ready)return;
 uint32_t request=p->request;p->request=0;
 if(request==1||request==3) {e->classic_mode=0;e->classic_request=0;sw_prepare(e,p->seed);}
 else {
  const sw_keep *k=&p->keeps[p->slot];if(!k->valid)return;
  e->classic_mode=(uint8_t)(k->classic_id!=0);e->classic_request=0;
  sw_command(e,SW_KEY,k->key);sw_command(e,SW_FORM,k->form);
  if(k->classic_id) {e->classic_choice=(uint8_t)(k->classic_id-1);sw_prepare_recall(e,k->seed,k->classic_id,k->bpm,k->swing);e->classic_request=0;}
  else sw_prepare_recall(e,k->seed,0,k->bpm,k->swing);
  sw_command(e,SW_BAND,k->players);sw_command(e,SW_MODERN,k->modern);sw_command(e,SW_HIPHOP,k->hiphop);sw_command(e,SW_AIR,k->air);sw_command(e,SW_GLOW,k->glow);sw_command(e,SW_SWELL,k->swell);sw_command(e,SW_FIDDLE_COLOUR,k->fiddle);sw_command(e,SW_TEXTURE,k->texture);sw_command(e,SW_AUTO_CALL,k->auto_bob);sw_command(e,SW_SOURCE_SOLOS,k->source_solos);
 }
 if(!e->playing&&e->pending_ready)sw_command(e,SW_MASTER,(int)(e->master_target*100.f+.5f));
}
void sw_panel_tick(sw_panel *p,uint32_t ms) {
 p->notice_ms=(uint16_t)(ms>=p->notice_ms?0:p->notice_ms-ms);
 p->status_ms=(uint16_t)(ms>=p->status_ms?0:p->status_ms-ms);
 p->fill_ms=(uint16_t)(ms>=p->fill_ms?0:p->fill_ms-ms);
 p->encore_ms=(uint16_t)(ms>=p->encore_ms?0:p->encore_ms-ms);
 p->idle_ms=(uint16_t)(ms>=SWP_IDLE_MS-p->idle_ms?SWP_IDLE_MS:p->idle_ms+ms);
 if((p->queued_kind&SWP_APPLIED)&&!p->status_ms)p->queued_kind=SWP_Q_NONE;
}
static const char level_notice[]="LEVEL";
void sw_panel_master_poll(sw_engine *e,int32_t level) {if(level>=0)sw_command(e,SW_MASTER,level);}
uint32_t sw_panel_level_notice(const sw_panel *p) {return p->notice_ms&&p->notice==level_notice;}
uint32_t sw_panel_then_now_notice(const sw_panel *p) {return p->notice_ms&&p->notice==then_now_notice;}
void sw_panel_observe(sw_engine *e,sw_panel *p) {
 uint32_t master=(uint32_t)(e->master_target*100.f+.5f);
 /* Ignore a one-percent boundary wobble while still updating the audio level.
    A real turn (or reaching zero/full) gets the normal LEVEL readout. */
 int master_delta=(int)master-(int)p->last_master;
 if(master!=p->last_master&&(master_delta<=-2||master_delta>=2||master==0||master==100)) {p->last_master=(uint8_t)master;p->notice=level_notice;p->notice_ms=1500;sw_panel_touch(p);}
 /* a tune that played to its end opens the encore window (the show shows ENCORE?) */
 if(p->was_playing&&!e->playing&&e->bar>=e->song.count)p->encore_ms=8000;
 p->was_playing=(uint8_t)e->playing;
 /* Keep the three performance pages armed until the player leaves. */
 if(p->page!=SWP_DIRECT&&p->page!=SWP_HOME&&p->page!=SWP_BOB&&p->idle_ms>=SWP_IDLE_MS&&!p->held_keys&&!p->buttons)page(e,p,SWP_DIRECT);
 uint32_t q=p->queued_kind;if(!q||(q&SWP_APPLIED))return;
 uint32_t ready=!p->request&&!e->classic_request&&!e->pending_ready,applied=0;
 switch(q) {
 case SWP_Q_SONG:applied=ready&&e->song.classic_id==p->queued_value&&(e->song_revision!=p->queued_rev||!e->playing);break;
 case SWP_Q_KEY:applied=ready&&e->song.key==p->queued_value;break;
 case SWP_Q_FORM:applied=ready&&!e->song.classic_id&&e->song.form==p->queued_value;break;
 case SWP_Q_END:applied=e->ending>=2;break;
 case SWP_Q_THEME:applied=!e->home_pending;break;
 case SWP_Q_LEON:applied=!e->leon_pending&&(e->solo_from!=0xffff||(!e->playing&&sw_call_active(e)));break;
 }
 if(applied) {p->queued_kind|=SWP_APPLIED;p->status_ms=1500;}
}
const char *sw_panel_knob_label(uint32_t page,uint32_t knob) {
 if(knob>3)return "";if(knob==3)return "AIR";
 if(page==SWP_TONE) {static const char *const s[]={"GLOW","SWELL","FIDDLE"};return s[knob];}
 if(!knob)return "TEMPO";
 if(knob==1)return page==SWP_SONG?"KEY":"SWING";
 return page==SWP_SONG||page==SWP_DIRECT?"FORM":page==SWP_BOB?"AUTO-BOB":page==SWP_BAND?"FUNK":"GLOW";
}
uint32_t sw_panel_knob_value(const sw_engine *e,const sw_panel *p,uint32_t knob) {
 if(knob==3)return (uint32_t)(e->air*100.f+.5f);
 if(p->page==SWP_TONE)return knob==0?(uint32_t)(e->glow*100.f+.5f):knob==1?(uint32_t)(e->swell*100.f+.5f):e->fiddle_colour;
 if(!knob)return e->bpm;if(knob==1)return p->page==SWP_SONG?e->key_next:e->swing;
 return p->page==SWP_SONG||p->page==SWP_DIRECT?e->form_next:p->page==SWP_BOB?e->auto_calls:p->page==SWP_BAND?(uint32_t)(e->hiphop_target*100.f+.5f):(uint32_t)(e->glow*100.f+.5f);
}
const char *sw_panel_status_label(const sw_engine *e,const sw_panel *p) {
 (void)e;static const char *const keys[]={"KEY C","KEY C#","KEY D","KEY EB","KEY E","KEY F","KEY F#","KEY G","KEY AB","KEY A","KEY BB","KEY B"};
 static const char *const forms[]={"FORM AABA","FORM BLUES","FORM REFRAIN"};
 switch(p->queued_kind&127u) {
 case SWP_Q_SONG:return p->queued_value?sw_classic_title(p->queued_value-1u):"NEW TUNE";
 case SWP_Q_KEY:return keys[p->queued_value%12u];case SWP_Q_FORM:return forms[p->queued_value%3u];
 case SWP_Q_END:return p->queued_kind&SWP_APPLIED?"ENDING":"ENDING - NEXT ROUND";case SWP_Q_THEME:return "THEME";case SWP_Q_LEON:return "LEON";default:return "";
 }
}
void sw_panel_buttons_poll(sw_engine *e,sw_panel *p,uint32_t pressed,uint32_t released,uint32_t levels) {
 uint32_t ups=(released|pressed|p->buttons)&~levels;
 /* A release/repress coalesced in one poll must be allowed to retrigger,
    while the currently held level must not be mistaken for a release. */
 p->buttons&=(uint16_t)~(pressed&released&levels);
 for(uint32_t i=0;i<14;i++)if(pressed&(1u<<i))sw_panel_edge(e,p,i,1);
 for(uint32_t i=0;i<14;i++)if(ups&(1u<<i))sw_panel_edge(e,p,i,0);
}
void sw_panel_keys_poll(sw_engine *e,sw_panel *p,uint32_t levels,uint32_t pressed,uint32_t previous) {
 uint32_t released=(previous|pressed)&~levels;
 for(uint32_t i=0;i<27;i++) {
  if(pressed&(1u<<i)) {
   /* A fresh debounced press while the previous poll still held this key
      proves a release/repress occurred between polls. Retrigger it; this is
      distinct from repeated browser key-down events, which stay idempotent. */
   if(previous&(1u<<i))sw_panel_key(e,p,i,0);
   sw_panel_key(e,p,i,1);
  }
  if(released&(1u<<i))sw_panel_key(e,p,i,0);
 }
}
uint32_t sw_panel_led(const sw_engine *e,const sw_panel *p,uint32_t id) {
 if(id>=41)return 0;
 if(id>=14) {
  uint32_t key=id-14;
  if(p->page==SWP_BOB)return key>=24||(p->mapped_family==p->family&&p->mapped_bank==p->bank&&p->call_slots[key]!=0xffff);
  if(p->page==SWP_BAND||p->page==SWP_DIRECT||p->page==SWP_KEEPS) {for(uint32_t i=0;i<8;i++)if(key==sw_panel_white(i)) {
    if(p->page==SWP_BAND)return (e->players>>i)&1u;if(p->page==SWP_KEEPS)return p->keeps[i].valid;
    uint32_t beat=e->playing&&((e->tick?e->tick-1u:0u)&2u)==0; /* toggles show state; queued cues blink */
    switch(i){case 0:return e->fill?1:beat;case 2:return e->leon_pending?beat:1;case 3:return e->home_pending?beat:1;case 4:return e->ending?beat:1;case 6:return e->auto_calls;case 7:return e->source_solos;default:return 1;}
   }return 0;}
  return (p->held_keys>>key)&1u;
 }
 switch(id) {
 case SWP_PLAY:return e->playing;case SWP_REC:return e->pending_ready||p->request;
 case SWP_FX:return e->fill;case SWP_SEL:return p->page==SWP_BAND;
 case SWP_ENV:return p->page==SWP_TONE;case SWP_LFO:return e->modern_target>=.5f;
 case SWP_EDIT:return p->page==SWP_SONG;case SWP_GLO:return p->page==SWP_BOB;
 case SWP_HOME_BUTTON:return p->page==SWP_DIRECT;case SWP_SAVE:return p->page==SWP_KEEPS;
 case SWP_ARP:return e->leon_pending||e->solo_from!=0xffff;
 case SWP_SEQ:return p->page==SWP_HOME;
 default:return p->octave!=0;
 }
}
