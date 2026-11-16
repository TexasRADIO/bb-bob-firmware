/* SPDX-License-Identifier: GPL-3.0-only; original small display and bitmap lettering. */
#include "display.h"
#include "bob_identity.h"
#include "rushing_font.h"
#include "rodeo.h"
#include "panel.h"
#ifdef SW_WITH_ART
#include "bob_art.h"
#endif
static const uint8_t letters[36][7]={
 {14,17,17,31,17,17,17},{30,17,17,30,17,17,30},{15,16,16,16,16,16,15},
 {30,17,17,17,17,17,30},{31,16,16,30,16,16,31},{31,16,16,30,16,16,16},
 {15,16,16,19,17,17,15},{17,17,17,31,17,17,17},{31,4,4,4,4,4,31},
 {7,2,2,2,18,18,12},{17,18,20,24,20,18,17},{16,16,16,16,16,16,31},
 {17,27,21,21,17,17,17},{17,25,21,19,17,17,17},{14,17,17,17,17,17,14},
 {30,17,17,30,16,16,16},{14,17,17,17,21,18,13},{30,17,17,30,20,18,17},
 {15,16,16,14,1,1,30},{31,4,4,4,4,4,4},{17,17,17,17,17,17,14},
 {17,17,17,17,17,10,4},{17,17,17,21,21,27,17},{17,17,10,4,10,17,17},
 {17,17,10,4,4,4,4},{31,1,2,4,8,16,31},
 {14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,2,4,8,31},
 {30,1,1,14,1,1,30},{2,6,10,18,31,2,2},{31,16,16,30,1,1,30},
 {14,16,16,30,17,17,14},{31,1,2,4,8,8,8},{14,17,17,14,17,17,14},{14,17,17,15,1,1,14}
};
static int rushing_letter(sw_rectangle r,void *c,int x,int y,char ch,int scale,uint16_t ink) {
 int index=ch>='A'&&ch<='Z'?ch-'A':ch>='0'&&ch<='9'?ch-'0'+26:-1;
 if(index<0)return 0;
 const sw_rushing_glyph *g=&sw_rushing_glyphs[index];int factor=scale/2;
 for(uint32_t row=0;row<SW_RUSHING_HEIGHT;row++)for(uint32_t col=0;col<g->width;) {
  uint32_t at=row*g->width+col;
  if(!(sw_rushing_bits[g->offset+at/8]&(128u>>(at%8)))) {col++;continue;}
  uint32_t start=col++;
  while(col<g->width) {at=row*g->width+col;if(!(sw_rushing_bits[g->offset+at/8]&(128u>>(at%8))))break;col++;}
  int left=x+(int)start*factor,right=x+(int)col*factor,top=y+(int)row*factor,bottom=top+factor;
  if(left<0)left=0;if(right>240)right=240;if(top<0)top=0;if(bottom>240)bottom=240;
  if(left<right&&top<bottom)r(left,top,right-left,bottom-top,ink,c);
 }
 return g->advance*factor;
}
static void text(sw_rectangle r,void *c,int x,int y,const char *s,int scale,uint16_t ink) {
 for(;*s;s++) {
  char ch=*s;if(ch>='a'&&ch<='z')ch=(char)(ch-'a'+'A');
  #ifndef SW_ART_PROBE
  /* The R14 packing oracle intentionally retains its historical typography;
     production headings and chord symbols use the original supplied font. */
  if(scale>=2) {int advance=rushing_letter(r,c,x,y,ch,scale,ink);if(advance) {x+=advance;continue;}}
  #else
  (void)rushing_letter;
  #endif
  int index=ch>='A'&&ch<='Z'?ch-'A':ch>='0'&&ch<='9'?ch-'0'+26:-1;
  if(index>=0)for(int j=0;j<7;j++)for(int k=0;k<5;k++)if(letters[index][j]&(16>>k))r(x+k*scale,y+j*scale,scale,scale,ink,c);
  if(*s=='-')r(x,y+3*scale,5*scale,scale,ink,c);
  if(*s=='_')r(x,y+6*scale,5*scale,scale,ink,c);
  if(*s=='!') {r(x+2*scale,y,scale,5*scale,ink,c);r(x+2*scale,y+6*scale,scale,scale,ink,c);}
  if(*s==':') {r(x+2*scale,y+1*scale,scale,2*scale,ink,c);r(x+2*scale,y+4*scale,scale,2*scale,ink,c);}
  if(*s=='/') for(int j=0;j<7;j++)r(x+(4-(j*5)/7)*scale,y+j*scale,scale,scale,ink,c);
  if(*s=='\'') r(x+2*scale,y,scale,2*scale,ink,c);
  if(*s=='.') r(x+2*scale,y+6*scale,scale,scale,ink,c);
  if(*s=='?') {r(x+scale,y,3*scale,scale,ink,c);r(x+4*scale,y+scale,scale,2*scale,ink,c);r(x+2*scale,y+3*scale,2*scale,scale,ink,c);r(x+2*scale,y+4*scale,scale,scale,ink,c);r(x+2*scale,y+6*scale,scale,scale,ink,c);}
  if(*s==',') {r(x+2*scale,y+5*scale,scale,2*scale,ink,c);r(x+scale,y+7*scale,scale,scale,ink,c);}
  if(*s=='>') for(int j=0;j<7;j++)r(x+(j<4?j:6-j)*scale+scale,y+j*scale,scale,scale,ink,c);
  if(*s=='<') for(int j=0;j<7;j++)r(x+(j<4?3-j:j-3)*scale+scale,y+j*scale,scale,scale,ink,c);
  if(*s=='#') {r(x+scale,y,scale,7*scale,ink,c);r(x+3*scale,y,scale,7*scale,ink,c);r(x,y+2*scale,5*scale,scale,ink,c);r(x,y+4*scale,5*scale,scale,ink,c);}
  x+=6*scale;
 }
}
static void number(char *out,uint32_t n) {char b[12];uint32_t i=0;do {b[i++]=(char)('0'+n%10);n/=10;}while(n);for(uint32_t j=0;j<i;j++)out[j]=b[i-j-1];out[i]=0;}
void sw_platform_display(const sw_platform_stats *s,sw_rectangle r,void *c) {
 const uint16_t cream=0xff78,teal=0x2a26,gold=0xe66e;
#if SW_HALF_FRAMES == 256u
 #define SW_DIAG_BUDGET "BUDGET 5804 US"
#else
 #define SW_DIAG_BUDGET "BUDGET 2902 US"
#endif
 const char *const labels[]={"AUDIO HALF PEAK US",SW_DIAG_BUDGET,"OVER BUDGET","DMA HALF MISSES","ENTRY GAP PEAK US","MASK BODY PEAK US","U STACK OF 24320","S STACK OF 7936","HALVES RENDERED"};
 uint32_t values[]={s->peak_ticks/24u,SW_HALF_TICKS/24u,s->over_budget,s->dma_misses,s->entry_peak_ticks/24u,s->mask_peak_ticks/24u,s->user_stack,s->irq_stack,s->halves};
 r(0,0,240,240,teal,c);text(r,c,8,8,SW_BOB_DISPLAY_VERSION,2,cream);
 text(r,c,8,27,SW_BOB_PRODUCT_ID,1,cream);text(r,c,110,27,"APP",1,cream);text(r,c,134,27,sw_bob_app_sha_prefix,1,gold);
 for(uint32_t i=0;i<9;i++) {char b[12];number(b,values[i]);text(r,c,8,43+(int)i*18,labels[i],1,cream);text(r,c,172,43+(int)i*18,b,1,gold);}
 text(r,c,8,216,"WALL TIME INCLUDES SCAN IRQ",1,cream);
 text(r,c,8,230,"RELEASE ENV LFO TO RETURN",1,cream);
}
#ifdef SW_WITH_ART
static void bitmap(sw_rectangle r,void *c,int x,int y,const sw_bob_image *image) {
 for(uint32_t row=0;row<image->height;row++) {
  int yy=y+(int)row;if(yy<0||yy>=240)continue;
  uint32_t at=image->rows[row],limit=image->rows[row+1],column=0;
  while(at<limit) {
   uint8_t token=image->runs[at++],pixel=token&31u;uint32_t end=column+(token>>5)+1u;
   /* Merge adjacent eight-pixel tokens back to the original rectangle span,
      preserving even the callback order/count, not just the final pixels. */
   while(at<limit&&(image->runs[at]&31u)==pixel)end+=(image->runs[at++]>>5)+1u;
   int left=x+(int)column,right=x+(int)end;if(left<0)left=0;if(right>240)right=240;
   if(pixel&&left<right)r(left,yy,right-left,1,sw_bob_palette[pixel],c);column=end;
  }
 }
}
#endif
/* Bob's corner portrait at any place: (x0, y0) is the 72 px frame's top-left. The corner is (164, 32). */
void sw_display_bob_at(const sw_engine *e,int x0,int y0,uint32_t frame,uint32_t speaking,sw_rectangle r,void *context) {
 #ifdef SW_WITH_ART
 const sw_bob_image *pose=sw_bob_portrait[frame&3u];
 uint32_t breath=(e->visual_ms/1100u)%4u;
 int y=y0+(breath==1?1:breath==3?-1:0);
 uint32_t gesture=e->gesture_active&&!speaking?e->gesture:0;
 if(e->gesture_active&&!speaking) {
  if(gesture==0)pose=sw_bob_portrait[(e->gesture_time/240)&1u];
  if(gesture==3||gesture==4)pose=sw_bob_gesture[(gesture==3?0:2)+((e->gesture_time/240)&1u)];
  if(gesture==5)y+=(e->gesture_time/250)%2?2:0;
  if(gesture==2)pose=sw_bob_portrait[1];
 }
 bitmap(r,context,x0,y,pose);
 /* Find the cigar ember in this exact pose instead of assuming a hand location. */
 int tip_x=pose->tip_x,tip_y=pose->tip_y;
 if(tip_x>=0) {
  int xx=x0+tip_x,yy=y+tip_y;
  r(xx,yy,1,1,sw_bob_palette[(e->visual_ms/350)%3==0?25:24],context);
  if(gesture==1)for(int i=0;i<3;i++) {
   int rise=(int)((e->gesture_time/85+i*6)%20),smoke_y=yy-rise-3;
   int smoke_x=xx-2+(int)((e->gesture_time/220+i)%3);
   if(smoke_y>=y0-2&&smoke_y<y0+76&&smoke_x>=x0&&smoke_x+2<=x0+76)r(smoke_x,smoke_y,2,1,sw_bob_palette[i==0?18:19],context);
  }
  if(gesture==2) {int ash_y=yy+(int)((e->gesture_time/70)%13);if(ash_y<y0+76)r(xx-1,ash_y,1,2,sw_bob_palette[17],context);}
 }
 #else
 (void)e;(void)x0;(void)y0;(void)frame;(void)speaking;(void)r;(void)context;
 #endif
}
void sw_display_bob(const sw_engine *e,uint32_t frame,uint32_t speaking,sw_rectangle r,void *context) {sw_display_bob_at(e,164,32,frame,speaking,r,context);}
/* Public helpers for the Showbill screens (src/showbill.c). */
void sw_text(sw_rectangle r,void *c,int x,int y,const char *s,int scale,uint16_t ink) {text(r,c,x,y,s,scale,ink);}
void sw_display_title_art(sw_rectangle r,void *c,int x,int y) {
 #ifdef SW_WITH_ART
 bitmap(r,c,x,y,&sw_bob_title);
 #else
 (void)r;(void)c;(void)x;(void)y;
 #endif
}

static void identity_stamp(const sw_engine *e,sw_rectangle r,void *c) {
 if(e->title) {
  /* The original96px logo and seated TEXAS artwork stay uncovered. */
  text(r,c,132,9,"APP",1,0x2a26);text(r,c,156,9,sw_bob_app_sha_prefix,1,0x2a26);
  text(r,c,216,9,SW_BOB_VERSION,1,0x2a26);
  text(r,c,132,22,SW_BOB_PRODUCT_ID,1,0x2a26);
 } else text(r,c,94,11,SW_BOB_VERSION,1,0x1144);
}
void sw_display(const sw_engine *e,sw_rectangle r,void *context) {
 const uint16_t cream=0xff78,teal=0x2a26,dark=0x1144,rust=0xb189,gold=0xe66e,dim=0x8ceb;
 #ifdef SW_ART_PROBE
 static const char *const pcs[]={"C","C#","D","EB","E","F","F#","G","AB","A","BB","B"};
 #endif
 static const char *const qualities[]={"6","7","-7","DIM","7#5","-6","MAJ7","9","AUG"};
 char b[12];uint32_t bar=e->bar<e->song.count?e->bar:e->song.count-1;
 sw_chord chord=e->song.bars[bar].chord[(e->tick>4)?1:0];
 if(e->record_active&&!e->record_release) {chord.root=e->song.key;chord.quality=0;}
 if(e->title) {
  r(0,0,240,240,cream,context);r(4,4,232,2,teal,context);r(4,236,232,2,teal,context);
  #ifdef SW_WITH_ART
  bitmap(r,context,12,8,&sw_bob_logo);
  #else
  text(r,context,12,11,"BB-BOB",2,teal);
  #endif
  text(r,context,12,34,"BOB WILLS IN YOUR POCKET",1,rust);
  #ifdef SW_WITH_ART
  bitmap(r,context,24,38,&sw_bob_title);
  #endif
  text(r,context,78,227,e->playing?"ON THE AIR!":"PLAY THE BAND",1,teal);identity_stamp(e,r,context);return;
 }
 r(0,0,240,240,teal,context);r(0,0,240,28,cream,context);
 text(r,context,12,7,"BB-BOB",2,dark);text(r,context,155,11,e->playing?"ON AIR":"READY",1,rust);
 text(r,context,12,36,e->pad_mode?"PLAY BOB":e->classic_mode&&e->song.bars[bar].section!=SW_SOLO?"CLASSICS":e->record_active?"LEON RECORD":e->solo_from!=0xffff?"LEON TAKES IT":sw_section_label(e),1,e->solo_from!=0xffff?gold:cream);
 text(r,context,112,36,e->song.classic_id?"SCORE":e->form_next==2?"REFRAIN":e->form_next?"BLUES":"AABA",1,dim);
 #ifdef SW_ART_PROBE
 text(r,context,112,49,pcs[e->key_next],1,gold);
 #else
 sw_rodeo_key_size(r,context,112,48,e->key_next,9,gold);
 #endif
 number(b,e->bpm);text(r,context,134,49,b,1,gold);
 #ifdef SW_ART_PROBE
 text(r,context,20,62,pcs[chord.root],4,cream);text(r,context,78,76,qualities[chord.quality],1,gold);
 #else
 int key_width=sw_rodeo_key(r,context,20,62,chord.root,cream);text(r,context,24+key_width,76,qualities[chord.quality],1,gold);
 #endif
 number(b,bar+1);text(r,context,112,62,"BAR",1,dim);text(r,context,112,76,b,2,cream);
 #ifdef SW_WITH_ART
 sw_display_bob(e,sw_bob_frame(e),sw_call_active(e),r,context);
 #endif
 for(uint32_t i=0;i<e->song.count;i++)r(12+(int)(i*216/e->song.count),110,(int)(216/e->song.count)>1?(int)(216/e->song.count)-1:1,4,i<=bar?gold:dark,context);
 if(e->pad_mode) {
  uint32_t start=e->call_page*16u,count=sw_call_count()-start;if(count>16)count=16;
  for(uint32_t i=0;i<count;i++) {int x=8+(i%4)*58,y=116+(i/4)*21;int on=sw_call_active(e)&&e->cue_kind==i+start;
   r(x,y,54,18,on?gold:dark,context);text(r,context,x+4,y+5,sw_call_short_label(i+start),1,on?teal:cream);}
  r(8,198,224,10,rust,context);text(r,context,18,199,sw_call_active(e)?sw_call_short_label(e->cue_kind):"TAP A BOB CALL",1,cream);
 } else {
 for(uint32_t i=0;i<8;i++) {int x=8+(i%4)*58,y=116+(i/4)*34;int on=e->players&(1u<<i);
  r(x,y,54,29,on?cream:dark,context);text(r,context,x+4,y+5,sw_player_label(e,i),1,on?teal:dim);
  for(uint32_t j=0;j<4;j++)r(x+5+j*11,y+19,8,3,on&&((e->tick/2+j)%4==0)?rust:on?gold:teal,context);
 }
 r(8,190,224,26,rust,context);
 text(r,context,18,200,e->leon_pending?"LEON - NEXT BAR!":sw_call_active(e)?(e->cue_kind==0?"TAKE IT AWAY!":e->cue_kind==1?"AH-HAAA!":"BOB CALL!"):e->classic_mode?(e->song.classic_id?sw_classic_title(e->song.classic_id-1):"CHOOSE A CLASSIC"):sw_score_short_label(e->score_choice),1,cream);
 }
 text(r,context,10,224,e->pad_mode?"MODE BAND / PLAY BOB":e->pending_ready?"NEW TUNE - NEXT BAR":e->classic_mode?(sw_classic_available(e->classic_choice)?"PRESETS SONG / REC NEW":"SELECTED SONG NOT READY"):"REC NEW  ARP SOLO  FX FILL",1,cream);
 if(!e->pad_mode) {r(8,183,224,3,dark,context);r(8,183,(int)(e->modern_target*224.f),3,gold,context);}
 #ifdef SW_WITH_ART
 if(e->walking&&!sw_call_active(e)) {
  r(0,208,240,32,teal,context);
  int x=-32+(int)(e->walk_clock*272u/(SW_RATE*8u));
  uint32_t frame=(e->walk_clock*e->bpm/(SW_RATE*15u))&7u;
  bitmap(r,context,x,208,sw_bob_walk[frame]);
 }
 #endif
 identity_stamp(e,r,context);
}
/* Physical240px surface. The historical artwork/portrait and base renderer
 * remain exact; the lower panel gives the active keys/knobs an explicit map. */
#ifdef SW_TRACE_PANEL_DISPLAY
extern void sw_trace_panel_text(const char *s,int x,int y);
#define PANEL_TRACE(s,x,y) sw_trace_panel_text(s,x,y)
#else
#define PANEL_TRACE(s,x,y) ((void)0)
#endif
static void panel_text(sw_rectangle r,void *c,int x,int y,const char *s,uint32_t max,uint16_t colour) {
 char b[37];uint32_t i=0;if(max>36)max=36;while(s[i]&&i<max) {b[i]=s[i];i++;}b[i]=0;PANEL_TRACE(b,x,y);text(r,c,x,y,b,1,colour);
}
static uint32_t song_row(uint32_t wanted) {
 uint32_t row=0;if(!wanted)return 0;
 for(uint32_t i=0;i<sw_classic_slots();i++)if(sw_classic_available(i)) {row++;if(i+1u==wanted)return row;}return 0;
}
static uint32_t row_song(uint32_t row) {
 if(!row)return 0;for(uint32_t i=0;i<sw_classic_slots();i++)if(sw_classic_available(i)&&!--row)return i+1u;return 0;
}

static void stage_header(const sw_engine *e,const sw_panel *p,sw_rectangle r,void *c,const char *context,uint32_t meter) {
 const uint16_t cream=0xff78,teal=0x2a26,dark=0x1144,rust=0xb189,gold=0xe66e,dim=0x8ceb;
 char b[24];uint32_t bar=e->bar<e->song.count?e->bar:e->song.count-1;
 sw_chord chord=e->song.bars[bar].chord[(e->tick>4)?1:0];
 r(0,0,240,105,teal,c);r(0,0,240,26,cream,c);
 sw_rodeo_text(r,c,10,7,sw_panel_page_name(p->page),1,dark);
 /* beat tally: four lamps, the downbeat in red */
 uint32_t beats=e->song.beats?e->song.beats:4u,beat=((e->tick?e->tick-1u:0u)/2u)%beats;
 for(uint32_t i=0;i<beats;i++)r(196+(int)i*10,9,7,7,!e->playing?0xd69au:i==beat?(i?gold:rust):0xd69au,c);
 {uint32_t nb=bar+1u<e->song.count?bar+1u:bar,coming=e->auto_calls&&e->playing&&e->song.bars[nb].section==SW_SOLO&&e->song.bars[bar].section!=SW_SOLO;
  /* the beat lamps double as the on-air tally; Bob's lamp takes their place when he calls */
  if(sw_call_active(e)||(coming&&beat%2u==0)) {r(192,5,44,16,rust,c);text(r,c,205,10,"BOB",1,cream);}}
 int kw=sw_rodeo_key(r,c,12,34,chord.root,cream);
 static const char *const qualities[]={"6","7","-7","DIM","7#5","-6","MAJ7","9","AUG"};
 text(r,c,16+kw,48,qualities[chord.quality],1,gold);
 const char *t=e->song.classic_id?sw_classic_title(e->song.classic_id-1):"ORIGINAL TUNE";
 {int n=0;while(t[n]&&n<23){b[n]=t[n];n++;}b[n]=0;}text(r,c,82,35,b,1,cream);
 text(r,c,82,48,"BAR",1,dim);number(b,bar+1);text(r,c,104,48,b,1,gold);
 text(r,c,124,48,e->score_playing&&e->score_arrangement?"FEATURE":sw_section_label(e),1,dim);
 for(uint32_t i=0;i<e->song.count;i++)r(12+(int)(i*216/e->song.count),66,(int)(216/e->song.count)>1?(int)(216/e->song.count)-1:1,4,i<=bar?gold:dark,c);
 if(meter) {
  /* THEN / NOW: the instrument's signature control, always visible */
  uint32_t k=(uint32_t)(e->modern_target*100.f+.5f);
  text(r,c,12,82,"THEN",1,k<50?cream:dim);text(r,c,206,82,"NOW",1,k>=50?cream:dim);
  r(42,82,160,8,dark,c);r(42,82,(int)(k*160/100),8,k<50?gold:0xe8f4,c);
  for(int i=1;i<4;i++)r(42+i*40,82,1,8,teal,c);
 } else text(r,c,12,82,context,1,gold);
}
void sw_panel_display(const sw_engine *e,const sw_panel *p,sw_rectangle r,void *c) {
 if(e->title) {sw_display(e,r,c);return;}
 const uint16_t cream=0xff78,teal=0x2a26,dark=0x1144,rust=0xb189,gold=0xe66e,dim=0x8ceb,mint=0x9f14;
 static const char *const keys[]={"C","C#","D","EB","E","F","F#","G","AB","A","BB","B"};
 static const char *const forms[]={"AABA","BLUES","REFRAIN"};
 char b[12];
 {char ctx[40]="";if(p->page==SWP_BOB){uint16_t sl[24];uint32_t n=sw_panel_call_slots(p,sl);const char *f=sw_panel_family_name(p->family);int i=0;while(f[i]){ctx[i]=f[i];i++;}ctx[i++]=' ';number(ctx+i,p->bank+1u);while(ctx[i])i++;ctx[i++]=' ';ctx[i++]='O';ctx[i++]='F';ctx[i++]=' ';number(ctx+i,(n+23)/24);}
  else if(p->page==SWP_KEEPS){const sw_keep *k=&p->keeps[p->slot%8u];const char *t=!k->valid?"EMPTY KEEP":k->classic_id?sw_classic_title(k->classic_id-1u):"ORIGINAL TUNE";int i=0;while(t[i]&&i<36){ctx[i]=t[i];i++;}ctx[i]=0;}
  stage_header(e,p,r,c,ctx,p->page!=SWP_BOB&&p->page!=SWP_KEEPS);}
 r(0,105,240,135,teal,c);
 if(p->page==SWP_BAND||p->page==SWP_KEEPS||p->page==SWP_DIRECT) {
  static const char *const cues[]={"FILL","BREAK","LEON","THEME","ENDING","ALL IN","AUTO BOB","SOLOS"};
  for(uint32_t i=0;i<8;i++) {
   int x=8+(int)(i%4)*58,y=117+(int)(i/4)*32;
   int on=p->page==SWP_BAND?((e->players>>i)&1u):p->page==SWP_KEEPS?p->keeps[i].valid:i==6?e->auto_calls:i==7?e->source_solos:1;
   int armed=p->page==SWP_DIRECT&&((i==0&&e->fill)||(i==1&&e->stop_bars)||(i==2&&e->leon_pending)||(i==3&&e->home_pending)||(i==4&&e->ending&&e->ending<2));
   r(x,y,54,28,armed?gold:on?cream:dark,c);number(b,i+1);panel_text(r,c,x+3,y+4,b,2,on?rust:dim);
   if(p->page==SWP_KEEPS) {panel_text(r,c,x+3,y+16,p->keeps[i].valid?"KEPT":"EMPTY",8,on?teal:dim);if(i==p->slot)r(x,y+26,54,2,gold,c);}
   else panel_text(r,c,x+3,y+16,p->page==SWP_BAND?sw_player_label(e,i):cues[i],8,on?teal:dim);
  }

 } else if(p->page==SWP_BOB) {
  uint16_t slots[24];sw_panel_call_slots(p,slots);

  for(uint32_t i=0;i<24;i++) {
   uint32_t index=slots[i];int x=8+(int)(i%4)*58,y=106+(int)(i/4)*11;
   int on=index!=0xffff,active=on&&sw_call_active(e)&&e->cue_kind==index;
   r(x,y,54,10,active?gold:dark,c);number(b,i+1u);panel_text(r,c,x+3,y+2,b,2,active?teal:gold);
   panel_text(r,c,x+23,y+2,on?sw_call_short_label(index):"----",4,active?teal:on?cream:dim);
  }
  panel_text(r,c,12,174,"25 LEON   26 AH-HA   27 RANDOM",36,gold);
 } else if(p->page==SWP_SONG) {
  uint32_t selected=song_row(p->cursor),total=sw_classic_ready_count()+1u,start=selected>2?selected-2:0;
  if(total>5&&start>total-5)start=total-5;
  for(uint32_t row=0;row<5&&start+row<total;row++) {
   uint32_t index=row_song(start+row),active=start+row==selected;int y=109+(int)row*13;
   uint16_t tempo=0;uint8_t key=0;r(8,y-2,224,11,active?cream:dark,c);
   panel_text(r,c,12,y,index?sw_classic_title(index-1u):"* NEW TUNE",24,active?teal:cream);
   if(index&&sw_classic_preview(index-1u,&key,&tempo)) {PANEL_TRACE(keys[key%12u],166,y);sw_rodeo_key_size(r,c,166,y,key,9,active?rust:gold);number(b,tempo);panel_text(r,c,196,y,b,3,active?rust:gold);}
  }
  panel_text(r,c,12,173,"PLAY PICKS - BROWSE IS SILENT",36,gold);
 } else {
  #ifdef SW_WITH_ART
  if(p->page==SWP_HOME&&e->walking&&!sw_call_active(e)) {
   int x=-32+(int)(e->walk_clock*272u/(SW_RATE*8u));
   uint32_t frame=(e->walk_clock*e->bpm/(SW_RATE*15u))&7u;
   panel_text(r,c,12,128,"27 NOTES - HOLD TO SUSTAIN",36,cream);bitmap(r,c,x,148,sw_bob_walk[frame]);
  } else
  #endif
  {
  panel_text(r,c,12,128,"27 NOTES - HOLD TO SUSTAIN",36,cream);
  panel_text(r,c,12,147,p->octave<0?"OCT -1 / F2-G4":p->octave>0?"OCT +1 / F4-G6":"OCT 0 / F3-G5",36,dim);
  panel_text(r,c,12,167,"PRESETS SONGS - REC NEW TUNE",36,gold);
  }
 }
 const char *line=p->page==SWP_BAND?"KEYS: PICK PLAYERS":p->page==SWP_KEEPS?"KEYS: KEEP; SAVE KEEPS; PLAY RECALL":p->page==SWP_DIRECT?"CUES 1-8 - OTHER KEYS PLAY":p->page==SWP_BOB?"KEYS: BOB CALLS":"KEYS: PLAY";
 panel_text(r,c,8,185,line,36,cream);
 if(p->page==SWP_BOB)panel_text(r,c,104,185,sw_panel_family_name(p->family),12,gold);
 else if(p->page==SWP_DIRECT)panel_text(r,c,170,185,sw_panel_player(p)==SW_BASS?"UPRIGHT":sw_player_label(e,sw_panel_player(p)),11,gold);
 else if(p->page!=SWP_BAND&&p->page!=SWP_KEEPS)panel_text(r,c,74,185,sw_panel_player(p)==SW_BASS?"UPRIGHT":sw_player_label(e,sw_panel_player(p)),14,gold);
 for(uint32_t i=0;i<4;i++) {
  int x=8+(int)i*58;r(x,197,54,26,dark,c);panel_text(r,c,x+2,199,sw_panel_knob_label(p->page,i),8,dim);
  uint32_t value=sw_panel_knob_value(e,p,i);number(b,value);
  const char *label=(p->page==SWP_SONG&&i==1)?keys[value%12u]:((p->page==SWP_SONG||p->page==SWP_DIRECT)&&i==2)?forms[value%3u]:(p->page==SWP_BOB&&i==2)?(value?"ON":"OFF"):b;
  if(p->page==SWP_SONG&&i==1) {PANEL_TRACE(label,x+3,212);sw_rodeo_key_size(r,c,x+3,210,value,11,gold);}
  else panel_text(r,c,x+3,212,label,8,gold);
 }
 if(sw_panel_then_now_notice(p)) {
  uint32_t k=(uint32_t)(e->modern_target*100.f+.5f);
  r(8,197,224,26,dark,c);text(r,c,14,203,"THEN",2,k<50?cream:dim);text(r,c,180,203,"NOW",2,k>=50?cream:dim);
  r(72,205,100,10,teal,c);r(72,205,(int)(k*100/100),10,k<50?gold:0xe8f4,c);
 }
 if(sw_panel_level_notice(p)) {panel_text(r,c,8,230,"LEVEL",5,cream);number(b,p->last_master);panel_text(r,c,50,230,b,3,gold);}
 else if(p->queued_kind) {
  uint32_t applied=p->queued_kind&SWP_APPLIED;r(0,227,240,13,applied?teal:rust,c);
  panel_text(r,c,8,230,applied?"APPLIED":"QUEUED",7,applied?mint:cream);panel_text(r,c,58,230,sw_panel_status_label(e,p),22,applied?mint:cream);
  if(!applied&&e->playing) {uint32_t beats=e->song.beats?e->song.beats:4u,beat=((e->tick?e->tick-1u:0u)/2u)%beats;
   char n[12]="IN ";number(n+3,beats-beat);panel_text(r,c,196,230,n,5,cream);}
 } else if(p->notice_ms&&!sw_panel_then_now_notice(p))panel_text(r,c,8,230,p->notice,36,cream);
}
