/* SPDX-License-Identifier: GPL-3.0-only
 * Showbill: BB-BOB as a show to watch. DORMANT when stopped; after SB_IDLE_MS untouched while
 * playing, LISTEN: Wes-Anderson-style sets (the Ballroom, On the Air, the road), a camera that
 * cuts on bar lines (Bob's reaction, a push-in on the soloist, back to the wide), one rising
 * note for every note the band really plays, and the players cut out as people.
 * Display-only: everything is read from the engine's public state; nothing is written to it. */
#include "showbill.h"
#include "art_bank.h"
#include "rodeo.h"
#ifdef SW_SHOWBILL_LINKED_ART
#include "showbill_art.h"
#else
typedef sb_image_t sb_image;
#endif
#include "showbill_calls.h"

#define CREAM 0xff78u
#define TEAL 0x2a26u
#define DARK 0x1144u
#define RUST 0xb189u
#define GOLD 0xe66eu
#define DIM 0x8cebu
#define PLUM 0x69c9u
#define PINK 0xedb7u
#define PINKD 0xdcf4u
#define BUTTER 0xf714u
#define POWDER 0xae3bu
#define MINT 0xbed8u
#define INK 0x1081u
#define LIGHT 0xf679u
#define MUSTARD 0xdd28u
#define TAN 0xcd0fu

static const sb_art *ART;
void sw_showbill_set_art(const sb_art *art) {ART=art;}
const sb_art *sw_showbill_art(void) {return ART;}
#define STAGE_(w,p) (ART?(const sb_image *)ART->stage[w][p]:0)
#define CORNER_(w,p) (ART?(const sb_image *)ART->corner[w][p]:0)
#define CLOSE_(w,p) (ART?(const sb_image *)ART->close[w][p]:0)
#define BG_(i) (ART?(const sb_image *)ART->bg[i]:0)
#ifdef SW_SHOWBILL_LINKED_ART
static const sb_image *const STAGE[SB_FIGURES][3]={{0,0,0},{&st_leon_cue,&st_leon_return,&st_leon_slide},{&st_eldon_up,&st_eldon_down,&st_eldon_return},
 {&st_al_keys,&st_al_lift,&st_al_return},{&st_herb_ready,&st_herb_pick,&st_herb_slide},{&st_tiny_ready,&st_tiny_pick,&st_tiny_return},
 {&st_tommy_rest,&st_tommy_small,&st_tommy_open},{&st_junior_ready,&st_junior_pick,&st_junior_return},
 {&st_noel_ready,&st_noel_pick,&st_noel_slide},{&st_jimmy_ready,&st_jimmy_pick,&st_jimmy_return}};
static const sb_image *const CORNER[SB_FIGURES][3]={{0,0,0},{&co_leon_cue,&co_leon_return,&co_leon_slide},{&co_eldon_up,&co_eldon_down,&co_eldon_return},
 {&co_al_keys,&co_al_lift,&co_al_return},{&co_herb_ready,&co_herb_pick,&co_herb_slide},{&co_tiny_ready,&co_tiny_pick,&co_tiny_return},
 {&co_tommy_rest,&co_tommy_small,&co_tommy_open},{&co_junior_ready,&co_junior_pick,&co_junior_return},
 {&co_noel_ready,&co_noel_pick,&co_noel_slide},{&co_jimmy_ready,&co_jimmy_pick,&co_jimmy_return}};
/* 120-px close-ups exist for Leon, Eldon and Al; everyone else pushes in on the 72-px corner pose at 3x. */
static const sb_image *const CLOSE[SB_FIGURES][3]={{0,0,0},{&cu_leon_cue,&cu_leon_return,&cu_leon_slide},{&cu_eldon_up,&cu_eldon_down,&cu_eldon_return},
 {&cu_al_keys,&cu_al_lift,&cu_al_return}};
static sb_art linked_art;
void sw_showbill_linked_art(void) {
 static const sb_image *const BGS[3]={&bg_ballroom,&bg_radio,&bg_road};
 for(int i=0;i<3;i++)linked_art.bg[i]=BGS[i];
 for(int w=0;w<SB_FIGURES;w++)for(int p=0;p<3;p++) {linked_art.stage[w][p]=STAGE[w][p];linked_art.corner[w][p]=CORNER[w][p];linked_art.close[w][p]=CLOSE[w][p];}
 linked_art.bob_speak=&cu_bob_speak;linked_art.bob_idle=&cu_bob_idle;ART=&linked_art;
}
#endif
static const uint8_t IDLE_POSE[SB_FIGURES]={0,1,2,2,0,0,0,0,0,0};
static const char *const NAME[SB_FIGURES]={"","LEON","ELDON","AL","HERB","TINY","TOMMY","JUNIOR","NOEL","JIMMY"};
static const char *const ROLE[SB_FIGURES]={"","ON STEEL","ON GUITAR","ON PIANO","ON STEEL","ON MANDOLIN","ON VOCALS","ON GUITAR","ON STEEL","ON GUITAR"};
/* Who takes a chair's next solo: steel and guitar rotate, so each song shows more of the band. */
static const uint8_t STEEL_ROTA[3]={SB_LEON,SB_HERB,SB_NOEL},GUITAR_ROTA[3]={SB_ELDON,SB_JUNIOR,SB_JIMMY};
static uint32_t steel_now(const sw_showbill *s) {for(uint32_t i=0;i<3;i++)if(s->who==STEEL_ROTA[i])return s->who;return SB_LEON;}
/* the guitarist on stage: the featured one, otherwise this song's turn in the rota (the one the stage draws) */
static uint32_t guitar_now(const sw_showbill *s) {for(uint32_t i=0;i<3;i++)if(s->who==GUITAR_ROTA[i])return s->who;return GUITAR_ROTA[s->guitar_turn%3u];}
static const char *const QUAL[9]={"6","7","-7","DIM","7#5","-6","MAJ7","9","AUG"};
/* A figure is on the bandstand only while its chair's player is switched on (BAND page). */
static uint32_t chair_of(uint32_t who) {
 for(uint32_t i=0;i<3;i++) {if(who==STEEL_ROTA[i])return SW_STEEL;if(who==GUITAR_ROTA[i]||who==SB_TINY)return SW_GUITAR;}
 return who==SB_AL?SW_PIANO:0xffu;
}
static uint32_t on_band(const sw_engine *e,uint32_t who) {uint32_t c=chair_of(who);return c==0xffu||((e->players>>c)&1u);}
/* Director style follows Then/Now: 0 broadcast (Then), 1 music television (Now). */
static uint32_t tv_style(const sw_engine *e) {return e->modern>=.5f;}
/* The set for this bar: road for intros, a home set for the head and solos (ballroom for dance
 * tempos, the studio for slow tunes), the other set for the bridge, the ballroom to finish.
 * In the Now style the head alternates sets every two bars, cut hard on the bar line. */
static uint32_t set_for(const sw_showbill *s,const sw_engine *e) {
 uint32_t bar=e->bar,b=bar<e->song.count?bar:e->song.count-1u,sec=e->song.bars[b].section,home=e->bpm>120?0u:1u;
 if((bar-s->song_bar0<2u&&bar<4u)||sec==SW_INTRO)return 2u;
 if(sec==SW_ENDING)return 0u;
 if(sec==SW_BRIDGE)return home?0u:1u;
 if(tv_style(e)&&s->now_alt)return home?0u:1u;
 return home;
}

/* ---------- drawing primitives ---------- */
static void box(sw_rectangle r,void *c,int x,int y,int w,int h,uint16_t ink) {
 if(x<0) {w+=x;x=0;} if(y<0) {h+=y;y=0;} if(x+w>240)w=240-x; if(y+h>240)h=240-y;
 if(w>0&&h>0)r(x,y,w,h,ink,c);
}
/* Row runs with the image's own palette; index 0 is transparent. scale draws each pixel as scale x scale. */
static void blit(sw_rectangle r,void *c,int x,int y,const sb_image *im,int scale) {
 if(!im)return;
 if(im->bank_entry) {sw_art_bank_blit(im,r,c,x,y,scale);return;}
 for(uint32_t row=0;row<im->height;row++) {
  int yy=y+(int)row*scale;if(yy>=240||yy+scale<=0)continue;
  uint32_t at=im->rows[row],limit=im->rows[row+1],col=0;
  while(at<limit) {
   uint8_t token=im->runs[at++],px=token&31u;uint32_t end=col+(token>>5)+1u;
   while(at<limit&&(im->runs[at]&31u)==px)end+=(im->runs[at++]>>5)+1u;
   if(px)box(r,c,x+(int)col*scale,yy,(int)(end-col)*scale,scale,im->palette[px-1]);
   col=end;
  }
 }
}
static int tw(const char *s,int scale) {int n=0;while(s[n])n++;return n?n*6*scale-scale:0;}
static void say(sw_rectangle r,void *c,int cx,int y,const char *s,int scale,uint16_t ink) {sw_text(r,c,cx-tw(s,scale)/2,y,s,scale,ink);}
static void ticket(sw_rectangle r,void *c,int x,int y,int w,int h,uint16_t ink,uint16_t ground) {
 box(r,c,x,y,w,h,ink);box(r,c,x,y,2,2,ground);box(r,c,x+w-2,y,2,2,ground);box(r,c,x,y+h-2,2,2,ground);box(r,c,x+w-2,y+h-2,2,2,ground);
}
/* no-bank forms: a two-tone set and a name card where a figure would stand */
static void plain_set(sw_rectangle r,void *c,uint16_t top,uint16_t floor) {box(r,c,0,0,240,140,top);box(r,c,0,140,240,100,floor);box(r,c,0,138,240,3,GOLD);}
static void name_card(sw_rectangle r,void *c,int cx,int y,uint32_t who) {
 ticket(r,c,cx-36,y,72,30,CREAM,PLUM);say(r,c,cx,y+5,NAME[who],1,RUST);say(r,c,cx,y+17,ROLE[who]+3,1,DARK);
}
static void stripes(sw_rectangle r,void *c) {for(int x=0;x<240;x+=12) {box(r,c,x,0,6,240,PINK);box(r,c,x+6,0,6,240,PINKD);}}
static void note_glyph(sw_rectangle r,void *c,int x,int y,uint16_t ink) {box(r,c,x,y+6,4,3,ink);box(r,c,x+3,y,1,8,ink);box(r,c,x+4,y+1,2,1,ink);box(r,c,x+5,y+2,1,2,ink);}
static void live_tally(sw_rectangle r,void *c) {box(r,c,8,8,46,14,RUST);box(r,c,13,12,6,6,0xfa49u);sw_text(r,c,24,11,"LIVE",1,CREAM);}
static void chord_bug(const sw_engine *e,sw_rectangle r,void *c,int x,int y,uint16_t ground) {
 const sw_bar *b=&e->song.bars[e->bar<e->song.count?e->bar:e->song.count-1];sw_chord ch=b->chord[e->tick>4?1:0];
 if(x>186)x=186;
 ticket(r,c,x,y,54,16,CREAM,ground);int kw=sw_rodeo_key_size(r,c,x+5,y+3,ch.root,11,DARK);sw_text(r,c,x+7+kw,y+4,QUAL[ch.quality%9],1,RUST);
}
static const char *song_title(const sw_engine *e) {return e->song.classic_id?sw_classic_title(e->song.classic_id-1u):"ORIGINAL TUNE";}
/* centred title clipped to a width, by truncating whole characters */
static void title_in(sw_rectangle r,void *c,const sw_engine *e,int cx,int y,int maxw,uint16_t ink) {
 char b[40];const char *s=song_title(e);int n=0,max=(maxw+1)/6;if(max>39)max=39;while(s[n]&&n<max) {b[n]=s[n];n++;}b[n]=0;say(r,c,cx,y,b,1,ink);
}

/* ---------- state ---------- */
void sw_showbill_init(sw_showbill *s) {uint8_t *b=(uint8_t *)s;for(uint32_t i=0;i<sizeof *s;i++)b[i]=0;for(uint32_t i=0;i<SB_FIGURES;i++)s->pose[i]=IDLE_POSE[i];s->song_revision=0xffffffffu;s->last_bar=0xffffu;}
void sw_showbill_on_screen(sw_showbill *s,uint32_t shown) {
 if(shown&&!s->on_screen&&s->curtain_pending) {s->curtain_pending=0;s->curtain_ms=600;}
 s->on_screen=(uint8_t)(shown!=0);
}
void sw_showbill_touch(sw_showbill *s) {s->idle_ms=0;}
void sw_showbill_prefer(sw_showbill *s,uint32_t pref) {s->pref=(uint8_t)(pref<3?pref:0);s->idle_ms=0;}
uint32_t sw_showbill_mode(const sw_showbill *s,const sw_engine *e) {
 if(!e->playing)return SB_DORMANT;
 if(s->pref==SB_PREF_PLAY)return SB_PLAY;
 if(s->pref==SB_PREF_WATCH)return SB_LISTEN;
 return s->idle_ms>=SB_IDLE_MS?SB_LISTEN:SB_PLAY;
}
uint32_t sw_showbill_panel_visible(const sw_showbill *s,const sw_engine *e,const sw_panel *p) {
 if((!e->title&&p->page!=SWP_HOME&&p->page!=SWP_DIRECT)||p->held_keys||p->buttons)return 1;
 /* a band cue hands the screen back to the show quickly */
 if(!e->title&&p->cue_touch&&p->idle_ms>=SWP_CUE_MS)return sw_showbill_mode(s,e)==SB_PLAY&&s->pref==SB_PREF_PLAY;
 if(!e->title&&p->idle_ms<SB_IDLE_MS)return 1;
 return sw_showbill_mode(s,e)==SB_PLAY;
}
/* MIDI note from a phase increment: count semitones from A4 (440 Hz at 44.1 kHz = 42852281). */
static uint8_t nearest_pitch(uint32_t increment) {
 float f=(float)increment,a=42852281.f;int note=69;
 while(f>a*1.0293022f&&note<127) {a*=1.0594631f;note++;}
 while(f<a*0.9715319f&&note>0) {a/=1.0594631f;note--;}
 return (uint8_t)note;
}
static void bump(sw_showbill *s,uint32_t who) {
 if(!who||who>=SB_FIGURES)return;
 if(who==SB_LEON||who==SB_HERB||who==SB_NOEL||who==SB_TOMMY)s->pose[who]=s->pose[who]==1?2:1; else s->pose[who]=s->pose[who]==0?1:0;
}
static uint32_t lead_from_notes(const sw_showbill *s) {
 uint32_t count[4]={0,0,0,0},best=SW_STEEL,n=0;
 for(uint32_t i=0;i<SB_NOTES;i++)if(s->notes[i].live&&s->notes[i].player<4)count[s->notes[i].player]++;
 for(uint32_t p=0;p<4;p++)if(count[p]>n) {n=count[p];best=p;}
 return best;
}
void sw_showbill_tick(sw_showbill *s,const sw_engine *e,uint32_t elapsed) {
 if(elapsed>1000)elapsed=1000;
 s->ms+=elapsed;if(s->idle_ms<0x7fffffffu)s->idle_ms+=elapsed;
 for(uint32_t i=0;i<SB_NOTES;i++)if(s->notes[i].live) {uint32_t a=s->notes[i].age+elapsed;if(a>=1300)s->notes[i].live=0;else s->notes[i].age=(uint16_t)a;}
 /* note-ons from the voices: a voice whose age is within the frames rendered since the last tick */
 uint32_t frames=e->rendered_frames-s->last_frames;s->last_frames=e->rendered_frames;
 /* each figure moves at most once per tick: a strum or chord starts several voices together, and
  * toggling once per voice cancelled out whenever the count was even (most guitar strums), which
  * left the guitarists frozen at the browser's ~93 ms and the device's 100 ms tick */
 uint32_t hit=0;
 if(frames&&frames<SW_RATE)for(uint32_t i=0;i<SW_VOICES;i++) {
  const sw_voice *v=&e->voice[i];
  if(!v->active||!v->age||v->age>frames)continue;
  uint8_t p=v->player;if(p==SW_BASS||p==SW_DRUMS)continue;
  sb_note *n=&s->notes[s->note_next++%SB_NOTES];uint8_t pitch=nearest_pitch(v->target);
  n->player=p;n->pitch=pitch;n->side=pitch&1u;n->age=0;n->live=1;
  hit|=1u<<(p==SW_STEEL?steel_now(s):p==SW_GUITAR?guitar_now(s):p==SW_PIANO?SB_AL:SB_NONE);
 }
 /* the steel chair comps on the beat when its part is resting (fiddle-led tunes give it no notes) */
 uint32_t steel=steel_now(s),beat=(e->tick?e->tick-1u:0u)/2u;
 if(hit&(1u<<steel))s->steel_quiet_ms=0; else if(s->steel_quiet_ms<60000u)s->steel_quiet_ms=(uint16_t)(s->steel_quiet_ms+elapsed);
 if(e->playing&&beat!=s->last_beat&&s->steel_quiet_ms>=600u)hit|=1u<<steel;
 s->last_beat=(uint8_t)beat;
 for(uint32_t w=1;w<SB_FIGURES;w++)if(hit&(1u<<w))bump(s,w);
 /* the camera: cuts only on bar lines */
 uint32_t bar=e->bar;
 if(e->song_revision!=s->song_revision) {if(s->song_revision!=0xffffffffu)s->guitar_turn++;s->song_revision=e->song_revision;s->song_bar0=(uint16_t)bar;s->shot=0;s->who=SB_NONE;}
 if(s->was_playing&&!e->playing) {if(s->on_screen)s->curtain_ms=600; else s->curtain_pending=1;if(e->bar>=e->song.count)s->encore_ms=6000;}
 if(e->playing)s->encore_ms=0; else if(s->encore_ms&&s->on_screen&&!s->curtain_ms)s->encore_ms=(uint16_t)(elapsed>=s->encore_ms?0:s->encore_ms-elapsed);
 if(s->wipe_ms)s->wipe_ms=(uint16_t)(elapsed>=s->wipe_ms?0:s->wipe_ms-elapsed);
 s->was_playing=(uint8_t)e->playing;
 if(s->curtain_ms)s->curtain_ms=(uint16_t)(elapsed>=s->curtain_ms?0:s->curtain_ms-elapsed);
 if(!e->playing) {s->shot=0;s->who=SB_NONE;s->in_solo=s->calling=0;return;}
 s->curtain_pending=0;
 uint32_t b=bar<e->song.count?bar:e->song.count-1u;
 uint32_t feature=e->solo_from!=0xffff&&bar>=e->solo_from&&bar<e->solo_until,tulsa=e->score_playing&&e->score_arrangement;
 uint32_t in_solo=feature||tulsa||e->song.bars[b].section==SW_SOLO,calling=sw_call_active(e);
 if(calling&&!s->calling) {
  uint32_t fig=e->cue_kind<SB_CALLS?sb_call_figure[e->cue_kind]:0u;
  s->shot=1;s->until_bar=(uint16_t)(bar+1);s->who=(uint8_t)fig;s->next=(uint8_t)(fig?2:0);s->shot_ms=s->ms;s->hold_bars=0;
 }
 if(in_solo&&!s->in_solo) {
  uint32_t chair=(feature||tulsa)?SW_STEEL:lead_from_notes(s),who=SB_NONE;
  if(chair==SW_STEEL)who=tulsa?SB_LEON:STEEL_ROTA[s->steel_turn++%3u]; else if(chair==SW_GUITAR)who=GUITAR_ROTA[s->guitar_turn++%3u]; else if(chair==SW_PIANO)who=SB_AL;
  if(who) {s->pose[who]=0;s->shot=1;s->until_bar=(uint16_t)(bar+1);s->who=(uint8_t)who;s->next=2;s->shot_ms=s->ms;s->hold_bars=0;}
  s->solo_bar0=(uint16_t)bar;
 }
 /* a solo trades fours along the chair, so the whole section gets seen; the Tulsa cue itself stays Leon's */
 else if(in_solo&&!tulsa&&!s->shot&&bar>=s->solo_bar0+4u) {
  uint32_t who=SB_NONE;
  for(uint32_t i=0;i<3;i++) {if(s->who==STEEL_ROTA[i])who=STEEL_ROTA[s->steel_turn++%3u];if(s->who==GUITAR_ROTA[i])who=GUITAR_ROTA[s->guitar_turn++%3u];}
  if(who&&who!=s->who) {s->pose[who]=0;s->shot=2;s->until_bar=(uint16_t)(bar+2);s->who=(uint8_t)who;s->next=0;s->shot_ms=s->ms;}
  s->solo_bar0=(uint16_t)bar;
 }
 /* the director: with no call or solo on camera, cut on the form (always on a bar line) */
 if(bar!=s->last_bar) {
  uint32_t sec=e->song.bars[b].section,was=s->last_section;
  if(!calling&&!in_solo&&!s->shot&&sec!=was&&s->last_bar!=0xffffu) {
   if(sec==SW_BRIDGE) { /* the bridge: a push-in on the rhythm section, alternating chairs */
    uint32_t who=(s->rhythm_turn++&1u)?SB_AL:GUITAR_ROTA[s->guitar_turn%3u];
    if(!on_band(e,who))who=who==SB_AL?GUITAR_ROTA[s->guitar_turn%3u]:SB_AL;
    if(on_band(e,who)) {s->pose[who]=0;s->shot=2;s->until_bar=(uint16_t)(bar+2);s->who=(uint8_t)who;s->next=0;s->shot_ms=s->ms;s->hold_bars=0;}
   } else if((sec==SW_RETURN&&was==SW_BRIDGE)||sec==SW_ENDING) { /* back to the head, or the last chorus: Bob reacts */
    s->shot=1;s->until_bar=(uint16_t)(bar+1);s->who=SB_NONE;s->next=0;s->shot_ms=s->ms;s->hold_bars=0;
   }
  }
  /* Now style: the head changes set every two bars */
  if(tv_style(e)&&!(bar&1u))s->now_alt^=1u;
  s->last_section=(uint8_t)sec;s->last_bar=(uint16_t)bar;
 }
 /* a shot that fired behind the controls waits (up to 8 bars) for the player to see it */
 if(s->shot&&!s->on_screen&&s->hold_bars<8u) {if(bar>=s->until_bar) {s->until_bar=(uint16_t)(bar+1);s->hold_bars++;}s->shot_ms=s->ms;}
 if(s->shot&&bar>=s->until_bar) {
  if(s->next==2&&s->who) {s->shot=2;s->until_bar=(uint16_t)(bar+2);s->next=0;s->shot_ms=s->ms;}
  else {s->shot=0;if(!in_solo)s->who=SB_NONE;}
 }
 if(!in_solo&&!s->shot)s->who=SB_NONE;
 s->in_solo=(uint8_t)in_solo;s->calling=(uint8_t)calling;
 /* a muted player is never the shot */
 if(s->shot==2&&s->who&&!on_band(e,s->who)) {s->shot=0;s->who=SB_NONE;}
 /* the broadcast style opens every new shot with an iris; the TV style just cuts */
 {uint32_t key=s->shot?(uint32_t)(16u+s->shot*16u+s->who):set_for(s,e);
  if(key!=s->scene_key) {s->scene_key=(uint8_t)key;if(!tv_style(e)&&s->on_screen)s->wipe_ms=360;}}
}

/* ---------- scenes ---------- */
static void notes_from(const sw_showbill *s,sw_rectangle r,void *c,int steel_x,int other_x,int y0,int spread) {
 static const uint16_t colour[8]={POWDER,GOLD,CREAM,RUST,0,0,BUTTER,MINT};
 for(uint32_t i=0;i<SB_NOTES;i++) {
  const sb_note *n=&s->notes[i];if(!n->live)continue;
  int from=n->player==SW_STEEL||n->player==SW_FIDDLE?steel_x:other_x,a=n->age;
  int x=from+(n->side?1:-1)*(spread+a*34/1300)-3,y=y0-((int)n->pitch-50)/2-a*44/1300;
  note_glyph(r,c,x,y,colour[n->player&7u]);
 }
}
static void stand(sw_rectangle r,void *c,const sb_image *im,int cx,int base) {if(im)blit(r,c,cx-(int)im->width/2,base-(int)im->height,im,1);}
static uint32_t centre_of(const sw_showbill *s) {return s->who?s->who:SB_LEON;}
static void bubble(sw_rectangle r,void *c,int x,int y,int w,const char *words) {
 box(r,c,x,y,w,14,CREAM);box(r,c,x+46,y+14,6,3,CREAM);box(r,c,x+48,y+17,3,2,CREAM);say(r,c,x+w/2,y+4,words,1,RUST);
}
static const char *call_words(const sw_engine *e,uint32_t named) {
 if(!sw_call_active(e))return 0;
 uint32_t fig=e->cue_kind<SB_CALLS?sb_call_figure[e->cue_kind]:0u,fam=e->cue_kind<SB_CALLS?sb_call_family[e->cue_kind]:0u;
 static char b[12];
 if(fig&&named) {int n=0;const char *s=NAME[fig];while(s[n]&&n<9) {b[n]=s[n];n++;}b[n++]='!';b[n]=0;return b;}
 return fam==1?"AH-HA!":fam==2?"YEAH!":0;
}
/* ---------- the dance floor: the Texas two-step ----------
 * Four couples dance quick-quick-slow-slow (one beat, one beat, two beats, two beats) and travel
 * anticlockwise round the floor, the line of dance: the near row moves right, the far row left.
 * The lead faces the line of dance and steps forward; the follow steps back in front of him.
 * Every fourth figure he raises his hand on the slows and she turns under it. In 3/4 they waltz:
 * one step a beat, rising on the second and third. Everything comes from the engine's beat clock
 * (bar, eighth tick, position in the tick), so frames are steady and the dance follows the tempo.
 * Nothing is stored; drawing reads the engine only. */
static void couple(sw_rectangle r,void *c,int cx,int base,int face,int lead_leg,int follow_leg,int rise,int turn) {
 /* drawn facing right around cx, mirrored for left; base is the floor line under the feet */
 #define PX(dx,y,w,h) box(r,c,face>0?cx+(dx):cx-(dx)-(w),(y),(w),(h),INK)
 int y=base-36-rise;
 /* lead: head, body, legs (the stepping leg reaches along the line of dance) */
 PX(-13,y,6,6);PX(-15,y+6,10,18);
 PX(-14+lead_leg,y+24,3,12+rise);PX(-9-(lead_leg>0?1:0),y+24,3,12+rise);
 /* the follow, a step ahead, facing him; her skirt flares as she turns */
 int fy=y+2,flare=turn?3:0,skirt=16+2*flare;
 PX(5,fy,6,6);PX(2,fy+6,12,14);PX(0-flare-follow_leg,fy+20,skirt,14+rise-2);
 /* joined hands, or his hand raised for her turn */
 if(turn) {PX(-7,y-4,3,12);PX(-7,y-6,12,3);}
 else PX(-5,y+10,8,3);
 #undef PX
}
/* FUNK up: the floor turns to 1982 electro. Solo dancers, wide stance, arms locking into a new
 * angle on every beat (the robot), a side step every bar. Same ink and scale as the couples. */
static void electro(sw_rectangle r,void *c,int cx,int base,uint32_t move,int face) {
 #define PX(dx,y,w,h) box(r,c,face>0?cx+(dx):cx-(dx)-(w),(y),(w),(h),INK)
 int y=base-36;PX(-3,y,6,6);PX(-5,y+6,10,16);
 PX(-6,y+22,3,14);PX(3,y+22,3,14);PX(-9,y+34,4,2);PX(5,y+34,4,2); /* wide stance */
 switch(move&3u) {
 case 0:PX(-13,y+8,8,3);PX(5,y+8,8,3);PX(-13,y-2,3,10);PX(10,y+10,3,8);break;   /* square arms */
 case 1:PX(5,y+6,12,3);PX(14,y-4,3,10);PX(-11,y+12,6,3);break;                  /* point up */
 case 2:PX(-13,y+8,8,3);PX(-13,y+10,3,10);PX(5,y+8,8,3);PX(10,y+10,3,10);break; /* arms down */
 default:PX(-17,y+7,12,3);PX(5,y+7,12,3);break;                                  /* wave */
 }
 #undef PX
}
static void dance_floor(const sw_engine *e,sw_rectangle r,void *c) {
 if(e->hiphop>=.5f) {
  uint32_t beats=e->song.beats?e->song.beats:4u,tick=e->tick?e->tick-1u:0u,beat=e->bar*beats+tick/2u;
  int shift=(e->bar&1u)?6:-6;
  for(int k=0;k<4;k++)electro(r,c,32+k*58+(k&1?-shift:shift),k&1?226:238,(uint32_t)(beat+(uint32_t)k),k&1?-1:1);
  return;
 }
 uint32_t beats=e->song.beats?e->song.beats:4u,tick=e->tick?e->tick-1u:0u;
 uint32_t beat=e->bar*beats+tick/2u,quarter=64u*(tick&1u);
 if(e->playing&&e->tick_length&&e->tick_remaining<=e->tick_length)quarter+=64u*(e->tick_length-e->tick_remaining)/e->tick_length;
 if(!e->playing)quarter=0;
 /* travel along the loop, in 1/128ths of a beat; a slow covers more floor than a quick */
 uint32_t step,phase,travel,turnfig=0;
 if(beats==3) {uint32_t b=beat%3u;step=b;phase=quarter;travel=(beat*128u+quarter)*5u/128u;}
 else {
  uint32_t fig=beat/6u,b=beat%6u;static const uint8_t START[6]={0,1,2,2,3,3},LEN[6]={1,1,2,2,2,2},AT[6]={0,0,0,1,0,1};
  step=START[b];phase=(AT[b]*128u+quarter)/LEN[b];
  static const uint8_t DONE[4]={0,6,12,22};static const uint8_t SIZE[4]={6,6,10,10};
  travel=fig*32u+DONE[step]+SIZE[step]*phase/128u;
  turnfig=(fig&3u)==3u&&step>=2u;
 }
 /* two rows, two couples each, 136 px apart: the far row travels left, the near row right, and a
  * couple leaving one side comes back on the other, as if round the far end of the hall */
 int order[4]={0,1,2,3},px[4],py[4],fc[4];
 for(int k=0;k<4;k++) {
  uint32_t at=(travel+(uint32_t)(k&1)*136u+(uint32_t)(k>>1)*68u)%272u;
  if(k<2) {px[k]=256-(int)at;py[k]=206;fc[k]=-1;}         /* far row: feet on 218 */
  else {px[k]=(int)at-16;py[k]=226;fc[k]=1;}               /* near row: feet on 238 */
 }
 for(int i=0;i<4;i++) {
  int k=order[i];
  /* quick steps sit a touch lower; slows glide; legs alternate step by step */
  int reach=e->playing?(phase<64u?(int)phase/16:(int)(128u-phase)/16):0;if(reach>3)reach=3;
  int lead=(step&1u)?reach:-reach/2,follow=(step&1u)?reach/2:-reach/2;
  int rise=beats==3?(step?1:0):(step>=2u&&phase>32u&&phase<96u?1:0);
  couple(r,c,px[k],py[k]+12,fc[k],lead,follow,rise,turnfig&&e->playing);
 }
}
/* NOW past halfway: a drum machine on the bandstand lip, its 16 step lamps in the classic
 * red/orange/yellow/cream groups, the lit lamp chasing the beat; past three quarters a silver
 * bass box with its two knobs joins it. Drawn from rectangles, no art bank space. */
static void now_machines(const sw_engine *e,sw_rectangle r,void *c) {
 if(e->modern<.5f)return;
 static const uint16_t lamp[4]={0xc0a3u,0xe3a3u,0xef00u,0xf79au};
 uint32_t tick=e->tick?e->tick-1u:0u,step=(e->bar*8u+tick)*2u%16u;
 box(r,c,150,139,62,13,INK);box(r,c,151,140,60,4,0x4208u);
 for(int i=0;i<16;i++) {box(r,c,152+i*3+(i/4)*1,146,2,4,(uint32_t)i==step?CREAM:lamp[i/4]);}
 if(e->modern>=.75f) {box(r,c,96,141,40,11,0xc618u);box(r,c,97,142,38,1,CREAM);box(r,c,100,145,4,4,INK);box(r,c,108,145,4,4,INK);
  for(int i=0;i<4;i++)box(r,c,116+i*4,147,3,3,(uint32_t)i==(step/4u)?RUST:0x8410u);}
}
/* The APPLAUSE sign, lit (radio set has it painted; the ballroom borrows the NO SPURS board). */
static void applause(sw_rectangle r,void *c,int x,int y,uint32_t lit) {
 box(r,c,x,y,54,13,lit?RUST:PLUM);box(r,c,x+1,y+1,52,1,lit?0xfa49u:PLUM);sw_text(r,c,x+3,y+3,"APPLAUSE",1,lit?CREAM:DIM);
}
static void ballroom(const sw_showbill *s,const sw_engine *e,sw_rectangle r,void *c) {
 if(BG_(0))blit(r,c,0,0,BG_(0),1); else plain_set(r,c,PINK,MINT);
 title_in(r,c,e,120,31,124,RUST);
 sw_display_bob_at(e,6,46,sw_bob_frame(e),sw_call_active(e),r,c);
 box(r,c,22,110,42,18,MINT);box(r,c,20,108,46,3,GOLD);
 uint32_t mid=centre_of(s),side=(mid==SB_ELDON||mid==SB_JUNIOR||mid==SB_JIMMY)?SB_AL:GUITAR_ROTA[s->guitar_turn%3u];
 {uint32_t xs[2]={120,196},ws[2]={mid,side};
  for(uint32_t i=0;i<2;i++) {int x=(int)xs[i];uint32_t w=ws[i];
   if(!on_band(e,w)) {box(r,c,x-9,116,18,4,0x8a22u);box(r,c,x-8,120,3,12,0x6181u);box(r,c,x+5,120,3,12,0x6181u);continue;} /* an empty stool */
   if(ART)stand(r,c,STAGE_(w,s->pose[w]%3),x,132); else name_card(r,c,x,112,w);}}
 const char *w=call_words(e,1);if(w)bubble(r,c,6,44,94,w);
 notes_from(s,r,c,120,196,104,34);
 now_machines(e,r,c);
 if(e->song.bars[e->bar<e->song.count?e->bar:e->song.count-1].section==SW_ENDING)applause(r,c,182,27,1);
 const sw_bar *b=&e->song.bars[e->bar<e->song.count?e->bar:e->song.count-1];sw_chord ch=b->chord[e->tick>4?1:0];
 box(r,c,88,160,34,20,INK);box(r,c,122,160,34,20,INK);box(r,c,88,169,68,1,0x39e7u);
 sw_rodeo_key_size(r,c,105-sw_rodeo_key_width_size(ch.root,14)/2,163,ch.root,14,CREAM);say(r,c,139,166,QUAL[ch.quality%9],1,CREAM);
 dance_floor(e,r,c);
 char bar[12]="BAR ";uint32_t n=e->bar+1u,k=4;char t[6];int m=0;do {t[m++]=(char)('0'+n%10);n/=10;} while(n&&m<5);while(m)bar[k++]=t[--m];bar[k]=0;sw_text(r,c,166,166,bar,1,BUTTER);
}
static void line(sw_rectangle r,void *c,int x0,int y0,int x1,int y1,uint16_t ink) {
 int dx=x1>x0?x1-x0:x0-x1,dy=y1>y0?y1-y0:y0-y1,sx=x0<x1?1:-1,sy=y0<y1?1:-1,err=dx-dy;
 for(;;) {box(r,c,x0,y0,2,2,ink);if(x0==x1&&y0==y1)break;int e2=2*err;if(e2>-dy) {err-=dy;x0+=sx;}if(e2<dx) {err+=dx;y0+=sy;}}
}
static void radio(const sw_showbill *s,const sw_engine *e,sw_rectangle r,void *c) {
 if(BG_(1))blit(r,c,0,0,BG_(1),1); else plain_set(r,c,TAN,DARK);
 /* the dial needle points at the chord's root: seven letters C D E F G A B over 0.8 pi each side */
 static const int8_t nx[12]={-15,-12,-10,-4,-1,6,9,12,15,16,17,17},ny[12]={-11,-13,-15,-18,-19,-18,-17,-15,-13,-11,-9,-6};
 const sw_bar *b=&e->song.bars[e->bar<e->song.count?e->bar:e->song.count-1];sw_chord ch=b->chord[e->tick>4?1:0];
 line(r,c,119,63,119+nx[ch.root%12],63+ny[ch.root%12],RUST);box(r,c,117,61,5,5,DARK);
 box(r,c,96,136,48,12,GOLD);title_in(r,c,e,120,139,44,PLUM);
 sw_display_bob_at(e,2,86,sw_bob_frame(e),sw_call_active(e),r,c);
 box(r,c,52,120,3,80,0xce79u);box(r,c,44,198,20,3,0xce79u);box(r,c,47,100,13,22,0xce79u);for(int i=0;i<5;i++)box(r,c,48,103+i*4,11,1,0x8c72u);
 notes_from(s,r,c,120,120,120,40);
 chord_bug(e,r,c,6,222,TAN);
 if(e->song.bars[e->bar<e->song.count?e->bar:e->song.count-1].section==SW_ENDING)applause(r,c,177,33,1);
}
static void road(const sw_showbill *s,const sw_engine *e,sw_rectangle r,void *c) {
 if(BG_(2))blit(r,c,0,0,BG_(2),1); else plain_set(r,c,POWDER,MUSTARD);
 title_in(r,c,e,120,8,224,BUTTER);
 notes_from(s,r,c,100,150,150,10);
 chord_bug(e,r,c,100,222,MUSTARD);
}
static void cone(sw_rectangle r,void *c) {for(int y=0;y<240;y+=2) {int l=70-70*y/240,w=100+140*y/240;box(r,c,l,y,w,2,LIGHT);}}
static void close_up(const sw_showbill *s,const sw_engine *e,sw_rectangle r,void *c) {
 uint32_t who=s->who;stripes(r,c);cone(r,c);
 int push=(int)(s->ms-s->shot_ms);if(push>900)push=900;int rise=30-push*30/900; /* the push-in, about two beats */
 const sb_image *im=CLOSE_(who,s->pose[who]%3);
 if(im) {int zoom=im->width<=72?3:2;blit(r,c,120-zoom*(int)im->width/2,252-zoom*(int)im->height+rise,im,zoom);}
 else {im=CORNER_(who,s->pose[who]%3);if(im)blit(r,c,120-3*(int)im->width/2,248-3*(int)im->height+rise,im,3);
  else {sw_text(r,c,120-tw(NAME[who],4)/2,90,NAME[who],4,PLUM);say(r,c,120,130,ROLE[who],2,DARK);}}
 notes_from(s,r,c,40,200,170,30);
 live_tally(r,c);box(r,c,0,196,200,30,PLUM);box(r,c,0,196,200,3,GOLD);
 sw_text(r,c,8,204,NAME[who],2,BUTTER);sw_text(r,c,16+tw(NAME[who],2),208,ROLE[who],1,CREAM);
 chord_bug(e,r,c,198,202,PLUM);
}
static void reaction(const sw_showbill *s,const sw_engine *e,sw_rectangle r,void *c) {
 stripes(r,c);box(r,c,0,0,240,20,PLUM);
 const sb_image *im=ART?(const sb_image *)(sw_call_active(e)?ART->bob_speak:ART->bob_idle):0;
 if(im)blit(r,c,120-(int)im->width,250-2*(int)im->height,im,2);
 else sw_display_bob_at(e,84,92,sw_bob_frame(e),sw_call_active(e),r,c);   /* the linked portrait */
 const char *w=call_words(e,1);if(!w&&s->next==2&&s->who)w="TAKE IT AWAY!";
 if(w) {box(r,c,14,34,212,28,CREAM);box(r,c,136,62,14,6,CREAM);box(r,c,138,68,6,5,CREAM);say(r,c,120,41,w,tw(w,3)<=200?3:2,RUST);}
 live_tally(r,c);box(r,c,0,210,240,30,PLUM);box(r,c,0,210,240,3,GOLD);sw_text(r,c,8,218,"BOB WILLS",2,BUTTER);sw_text(r,c,150,222,"BANDLEADER",1,CREAM);
}
/* DORMANT alternates the title card with MEET THE BAND: nine players, three to a page, grouped by
 * chair, so a stopped unit introduces the band. No instructions are painted over the art: the
 * controls are the instrument's own. */
static const uint8_t BAND_PAGE[3][3]={{SB_LEON,SB_ELDON,SB_AL},{SB_HERB,SB_NOEL,SB_TINY},{SB_TOMMY,SB_JUNIOR,SB_JIMMY}};
static const char *const PART[SB_FIGURES]={"","STEEL","GUITAR","PIANO","STEEL","MANDOLIN","VOCALS","GUITAR","STEEL","GUITAR"};
static void dormant_frame(const sw_showbill *s,sw_rectangle r,void *c) {
 uint32_t lit=s->ms/600u;
 for(int i=0;i<30;i++) {uint16_t k=(uint32_t)(i+(int)lit)%3u==0?RUST:GOLD;box(r,c,3+i*8,3,3,3,k);box(r,c,3+i*8,234,3,3,k);}
 for(int i=1;i<29;i++) {uint16_t k=(uint32_t)(i+(int)lit)%3u==0?RUST:GOLD;box(r,c,3,3+i*8,3,3,k);box(r,c,234,3+i*8,3,3,k);}
}
static uint32_t lit_pose(const sw_showbill *s,uint32_t i);
typedef struct {sw_rectangle r;void *c;} sb_grey;
static void grey_rect(int x,int y,int w,int h,uint16_t ink,void *ctx) {sb_grey *g=ctx;int l=(((ink>>11)&31)*2+((ink>>5)&63)+(ink&31)*2)/6;l=12+l/2;g->r(x,y,w,h,(uint16_t)((l<<11)|((l*2)<<5)|l),g->c);}
static void meet_the_band(const sw_showbill *s,const sw_engine *e,sw_rectangle r,void *c,uint32_t page) {
 for(int x=0;x<240;x+=12) {box(r,c,x,0,6,150,PINK);box(r,c,x+6,0,6,150,PINKD);}
 box(r,c,0,150,240,90,MINT);box(r,c,0,148,240,3,GOLD);
 dormant_frame(s,r,c);
 say(r,c,121,15,"MEET THE BAND",2,CREAM);say(r,c,120,14,"MEET THE BAND",2,RUST);
 for(uint32_t i=0;i<3;i++) {
  uint32_t who=BAND_PAGE[page%3u][i];int cx=44+(int)i*76;
  /* a small nod on the beat of the title lights, so the card feels alive */
  const sb_image *im=CORNER_(who,(lit_pose(s,i))%3);uint32_t in=on_band(e,who);
  if(im) {if(in)blit(r,c,cx-(int)im->width/2,146-(int)im->height,im,1);
   else {sb_grey g={r,c};blit(grey_rect,&g,cx-(int)im->width/2,146-(int)im->height,im,1);}}
  ticket(r,c,cx-34,158,68,13,in?CREAM:0xc618u,MINT);say(r,c,cx,161,NAME[who],1,in?RUST:DIM);say(r,c,cx,176,in?PART[who]:"SITTING OUT",1,DARK);
 }
 say(r,c,120,198,"THE TEXAS PLAYBOYS",1,PLUM);
 for(uint32_t p=0;p<3;p++)box(r,c,108+(int)p*10,216,6,3,p==page%3u?RUST:0x9cd3u);
}
static uint32_t lit_pose(const sw_showbill *s,uint32_t i) {return (s->ms/700u+i)%4u==0?1u:0u;}
static void dormant(const sw_showbill *s,const sw_engine *e,sw_rectangle r,void *c) {
 uint32_t phase=(s->ms/1000u)%18u;
 if(phase>=6u) {meet_the_band(s,e,r,c,(phase-6u)/4u);return;}
 box(r,c,0,0,240,240,CREAM);dormant_frame(s,r,c);
 say(r,c,121,11,"BB-BOB",3,RUST);say(r,c,120,10,"BB-BOB",3,DARK);
 /* the art stands clear: no panel over it; the song's name sits on the line beneath */
 sw_display_title_art(r,c,24,32);
 title_in(r,c,e,120,226,212,RUST);
}
typedef struct {sw_rectangle r;void *c;uint32_t k;} sb_tint;
static uint16_t tint_colour(uint16_t v,uint32_t k) {
 int R=(v>>11)&31,G=(v>>5)&63,B=v&31,l=(R*2+G+B*2)/6; /* 0..31-ish luminance */
 /* NOW: deep violet shadows -> hot pink mids -> cyan/white highlights */
 int nr=l<12?6+l:l<24?31:20+(31-l),ng=l<12?2+l:l<24?(l-12)*2:40+(l-24)*3,nb=l<12?12+l:l<24?20:31;
 if(ng>63)ng=63;if(nr>31)nr=31;
 R=R+(nr-R)*(int)k/64;G=G+(ng-G)*(int)k/64;B=B+(nb-B)*(int)k/64;
 return (uint16_t)((R<<11)|(G<<5)|B);
}
static void tint_rect(int x,int y,int w,int h,uint16_t ink,void *ctx) {sb_tint *t=ctx;t->r(x,y,w,h,tint_colour(ink,t->k),t->c);}
static void showbill_draw_plain(const sw_showbill *s,const sw_engine *e,sw_rectangle r,void *c);
void sw_showbill_draw(const sw_showbill *s,const sw_engine *e,sw_rectangle r,void *c) {
 uint32_t k=(uint32_t)(e->modern*64.f+.5f);
 if(k<4||sw_showbill_mode(s,e)==SB_DORMANT) {showbill_draw_plain(s,e,r,c);return;}
 sb_tint t={r,c,k>64?64:k};showbill_draw_plain(s,e,tint_rect,&t);
}
static void showbill_draw_plain(const sw_showbill *s,const sw_engine *e,sw_rectangle r,void *c) {
 uint32_t mode=sw_showbill_mode(s,e);
 if(mode==SB_DORMANT) {
  if(s->curtain_ms) { /* the curtain closes on the set, then the showbill returns */
   int w=(int)(600u-s->curtain_ms)*120/600;
   if(BG_(0))blit(r,c,0,0,BG_(0),1); else plain_set(r,c,PINK,MINT);
   for(int x=0;x<w;x+=10) {box(r,c,x,0,8,240,RUST);box(r,c,x+8,0,2,240,PLUM);box(r,c,239-x-8,0,8,240,RUST);box(r,c,239-x-10,0,2,240,PLUM);}
   box(r,c,0,0,240,14,GOLD);return;
  }
  if(s->encore_ms) { /* the band finished the tune: applause, and an encore if the player asks */
   if(BG_(1))blit(r,c,0,0,BG_(1),1); else plain_set(r,c,TAN,DARK);
   applause(r,c,177,33,(s->ms/250u)&1u);
   ticket(r,c,40,150,160,46,CREAM,TAN);say(r,c,120,158,"ENCORE?",3,RUST);say(r,c,120,184,"PRESS PLAY",1,DARK);
   sw_display_bob_at(e,2,86,0,0,r,c);return;
  }
  dormant(s,e,r,c);return;
 }
 /* reached only when the panel has handed the screen to the show (idle, or soon after a cue) */
 uint32_t tv=tv_style(e),set=set_for(s,e);
 if(s->shot==2&&s->who&&!tv) close_up(s,e,r,c);
 else if(s->shot==1) reaction(s,e,r,c);
 else {
  if(set==2)road(s,e,r,c); else if(set==1)radio(s,e,r,c); else ballroom(s,e,r,c);
  /* music TV: the soloist rides in a picture-in-picture box instead of a full push-in */
  if(tv&&s->shot==2&&s->who) {const sb_image *im=CORNER_(s->who,s->pose[s->who]%3);
   box(r,c,150,14,84,96,CREAM);box(r,c,152,16,80,92,INK);
   if(im)blit(r,c,192-(int)im->width/2,104-(int)im->height,im,1);
   box(r,c,152,96,80,12,0xe8f4u);sw_text(r,c,156,99,NAME[s->who],1,CREAM);live_tally(r,c);}
 }
 if(tv) {
  /* the title strip for the first two bars of a tune, then scanlines over everything */
  if(e->bar-s->song_bar0<4u&&set!=2u) {box(r,c,0,176,240,26,0xe8f4u);box(r,c,0,176,240,2,CREAM);title_in(r,c,e,120,185,228,CREAM);}
  uint32_t gap=e->modern>=.75f?4u:6u;for(uint32_t y=1;y<240;y+=gap)box(r,c,0,(int)y,240,1,0x3006u);
 } else {
  /* the broadcast: rounded film corners, and an iris opening on every new shot */
  for(int i=0;i<6;i++) {int w=6-i;box(r,c,0,i,w,1,INK);box(r,c,240-w,i,w,1,INK);box(r,c,0,239-i,w,1,INK);box(r,c,240-w,239-i,w,1,INK);}
  if(s->wipe_ms) {int R=(int)(360u-s->wipe_ms)*175/360;
   for(int y=0;y<240;y+=2) {int dy=y-120,hw;if(dy*dy>=R*R) {box(r,c,0,y,240,2,INK);continue;}
    hw=0;while((hw+1)*(hw+1)+dy*dy<R*R)hw++;if(120-hw>0)box(r,c,0,y,120-hw,2,INK);if(120+hw<240)box(r,c,120+hw,y,120-hw,2,INK);}}
 }
}
