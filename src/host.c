/* SPDX-License-Identifier: GPL-3.0-only; host adapter runs the firmware engine. */
#include "swing.h"
#include "display.h"
#include "showbill.h"
#include "art_bank.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
static sw_engine host;
static sw_panel panel;
static sw_showbill showbill;
static sw_art_bank packed_art;
static uint8_t art_bank_attempted;
static uint8_t panel_enabled;
static uint16_t screen[240*240];
void sw_status_json(const sw_engine *e,char *out,uint32_t capacity) {
 uint32_t b=e->bar<e->song.count?e->bar:e->song.count-1;
 snprintf(out,capacity,"{\"seed\":%u,\"playing\":%u,\"bar\":%u,\"bars\":%u,\"tick\":%u,\"bpm\":%u,\"swing\":%u,\"players\":%u,\"modern\":%u,\"air\":%u,\"glow\":%u,\"swell\":%u,\"key\":%u,\"form\":%u,\"nextKey\":%u,\"nextForm\":%u,\"pending\":%u,\"leonPending\":%u,\"solo\":%u,\"revision\":%u,\"section\":\"%s\",\"steals\":%u,\"frames\":%u}",
 e->song.seed,e->playing,e->bar,e->song.count,e->tick,e->bpm,e->swing,e->players,(unsigned)(e->modern_target*100+.5f),(unsigned)(e->air*100+.5f),(unsigned)(e->glow*100+.5f),(unsigned)(e->swell*100+.5f),e->song.key,e->song.form,e->key_next,e->form_next,e->pending_ready,e->leon_pending,e->solo_from!=0xffff,e->song_revision,sw_section_label(e),e->voice_steals,e->rendered_frames);
 uint32_t used=(uint32_t)strlen(out);
 if(used&&used+320<capacity)snprintf(out+used-1,capacity-used+1,",\"call\":%u,\"callActive\":%u,\"callCount\":%u,\"historicCalls\":%u,\"spoken\":%u,\"bobFrame\":%u,\"title\":%u,\"autoBob\":%u,\"padMode\":%u,\"leonRecord\":%u,\"leonRecordPending\":%u,\"leonRecordAvailable\":%u}",e->cue_kind,sw_call_active(e),sw_call_count(),sw_calls_historic(),e->spoken,sw_bob_frame(e),e->title,e->auto_calls,e->pad_mode,e->record_active,e->record_pending,sw_record_available());
 used=(uint32_t)strlen(out);
 if(used&&used+50<capacity)snprintf(out+used-1,capacity-used+1,",\"callTargets\":%u}",sw_call_targets(e));
 used=(uint32_t)strlen(out);
 if(used&&used+190<capacity)snprintf(out+used-1,capacity-used+1,",\"sampleCount\":%u,\"sampleSelected\":%u,\"sampleActive\":%u,\"samplePosition\":%u,\"sampleTargets\":%u,\"samplePlaying\":%u}",sw_sample_count(),sw_sample_selected(e),sw_sample_active(e),sw_sample_position(e),sw_sample_targets(e),sw_sample_playing(e));
 used=(uint32_t)strlen(out);
 if(used&&used+190<capacity)snprintf(out+used-1,capacity-used+1,",\"nowApplied\":%u,\"acidNotes\":%u,\"acidSlides\":%u,\"danceSteps\":%u,\"padNotes\":%u,\"arpNotes\":%u}",(unsigned)(e->modern*100+.5f),e->acid_notes,e->acid_slides,e->dance_steps,e->pad_notes,e->arp_notes);
 used=(uint32_t)strlen(out);
 if(used&&used+230<capacity)snprintf(out+used-1,capacity-used+1,",\"scorePlaying\":%u,\"scoreSelected\":%u,\"scoreCount\":%u,\"scoreNotes\":%u,\"sourceSolos\":%u,\"texture\":%u,\"callPage\":%u,\"phraseCell\":%u,\"fiddleColour\":%u}",e->score_playing,e->score_choice,sw_score_count(),e->score_notes,e->source_solos,e->texture,e->call_page,e->song.bars[b].phrase_id,e->fiddle_colour);
 used=(uint32_t)strlen(out);
 if(used&&used+260<capacity)snprintf(out+used-1,capacity-used+1,",\"classicMode\":%u,\"classicChoice\":%u,\"classicId\":%u,\"classicsReady\":%u,\"classicNotes\":%u,\"beats\":%u,\"classicTitle\":\"%s\"}",e->classic_mode,e->classic_choice,e->song.classic_id,sw_classic_ready_count(),e->classic_notes,e->song.beats,e->song.classic_id?sw_classic_title(e->song.classic_id-1):"Original tune");
}
static void host_prepare(void) {
 sw_panel_service(&host,&panel);
 if(host.classic_request&&!host.pending_ready) {
  uint32_t request=host.classic_request;host.classic_request=0;
  if(request==1)sw_prepare_classic(&host,host.classic_choice);else sw_prepare(&host,host.song.seed+1);
  if(!host.playing&&host.pending_ready)sw_command(&host,SW_MASTER,(int32_t)(host.master_target*100.f+.5f));
 }
 if(panel_enabled)sw_panel_observe(&host,&panel);
}
static void host_showbill_init(void) {
 sw_showbill_init(&showbill);
 if(packed_art.bank.bytes) {sw_showbill_set_art(&packed_art.art);return;}
 if(art_bank_attempted) {sw_showbill_set_art(0);return;}
 #ifdef SW_SHOWBILL_LINKED_ART
 sw_showbill_linked_art();
 #else
 sw_showbill_set_art(0);
 #endif
}
/* Immutable storage belongs to the caller until it detaches/replaces it. */
int sw_host_art_bank(const uint8_t *bytes,uint32_t length) {
 sw_showbill_set_art(0);art_bank_attempted=1;
 int ready=sw_art_bank_open(&packed_art,bytes,length,0);
 if(ready)sw_showbill_set_art(&packed_art.art);return ready;
}
#ifdef SW_WITH_BANK
/* Called before audio starts. Reset removes queued old-bank cues and shuffle
 * indices before the caller may release the previous immutable bank storage. */
int sw_host_calls_bank(const uint8_t *bytes,uint32_t length,uint32_t limit) {
 sw_init(&host,301026);sw_panel_init(&panel);host_showbill_init();panel_enabled=0;
 int ready=sw_calls_bank(bytes,length,limit);sw_panel_init(&panel);return ready;
}
#endif
void sw_host_reset(uint32_t seed) {sw_init(&host,seed);sw_panel_init(&panel);host_showbill_init();panel_enabled=0;}
void sw_host_panel_enable(uint32_t enabled) {sw_panel_release(&host,&panel);panel_enabled=(uint8_t)(enabled!=0);}
void sw_host_panel_control(uint32_t kind,uint32_t index,int32_t value) {
 if(!panel_enabled)return;sw_showbill_touch(&showbill);
 if(kind==0)sw_panel_edge(&host,&panel,index,value!=0);
 if(kind==1)sw_panel_encoder(&host,&panel,index,value);
 if(kind==2)sw_panel_key(&host,&panel,index,value!=0);
 if(kind==3)sw_panel_release(&host,&panel);
 if(kind==4) {sw_panel_touch(&panel);sw_command(&host,SW_MASTER,value);}
 host_prepare();
}
void sw_host_command(uint32_t command,int32_t value) {
 sw_command(&host,command,value);
 if(panel_enabled) {sw_showbill_touch(&showbill);sw_panel_touch(&panel);
  if(command==SW_PAD_MODE) {sw_panel_release(&host,&panel);panel.page=value?SWP_BOB:SWP_DIRECT;}
  if(command==SW_CLASSIC_MODE&&panel.page==SWP_BOB) {sw_panel_release(&host,&panel);panel.page=SWP_DIRECT;}
  if(command==SW_PLAY&&!host.playing)sw_panel_release(&host,&panel);
 }
 host_prepare();
}
void sw_host_visual_advance(uint32_t milliseconds) {sw_visual_advance(&host,milliseconds);sw_panel_tick(&panel,milliseconds);if(panel_enabled) {sw_panel_observe(&host,&panel);sw_showbill_tick(&showbill,&host,milliseconds);}}
void sw_host_render(float *stereo,uint32_t frames) {host_prepare();sw_render(&host,stereo,frames);}
const char *sw_host_state(void) {
 static char buffer[2560];host_prepare();sw_status_json(&host,buffer,sizeof buffer);
 size_t used=strlen(buffer);
 if(used&&used+60<sizeof buffer)snprintf(buffer+used-1,sizeof buffer-used+1,",\"hiphop\":%u,\"hiphopApplied\":%u}",(unsigned)(host.hiphop_target*100.f+.5f),(unsigned)(host.hiphop*100.f+.5f));
 used=strlen(buffer);
 if(panel_enabled&&used+600<sizeof buffer) {
  uint32_t button_leds=0,key_leds=0;
  for(uint32_t i=0;i<41;i++)if(sw_panel_led(&host,&panel,i)) {if(i<14)button_leds|=1u<<i;else key_leds|=1u<<(i-14);}
  snprintf(buffer+used-1,sizeof buffer-used+1,",\"panel\":{\"page\":%u,\"voice\":%u,\"player\":%u,\"octave\":%d,\"bank\":%u,\"slot\":%u,\"held\":%u,\"keeps\":%u,\"buttons\":%u,\"master\":%u,\"cursor\":%u,\"family\":%u,\"queued\":%u,\"showbillMode\":%u,\"showbillWho\":%u,\"showbillShot\":%u,\"showbillPanel\":%u,\"artBankReady\":%u,\"ledButtons\":%u,\"ledKeys\":%u}}",panel.page,panel.voice,sw_panel_player(&panel),panel.octave,panel.bank,panel.slot,panel.held_keys,(unsigned)(panel.keeps[0].valid|(panel.keeps[1].valid<<1)|(panel.keeps[2].valid<<2)|(panel.keeps[3].valid<<3)|(panel.keeps[4].valid<<4)|(panel.keeps[5].valid<<5)|(panel.keeps[6].valid<<6)|(panel.keeps[7].valid<<7)),panel.buttons,(unsigned)(host.master_target*100.f+.5f),panel.cursor,panel.family,panel.queued_kind,sw_showbill_mode(&showbill,&host),showbill.who,showbill.shot,sw_showbill_panel_visible(&showbill,&host,&panel),packed_art.bank.bytes!=0,button_leds,key_leds);
  used=strlen(buffer);uint16_t slots[24];sw_panel_call_slots(&panel,slots);
  snprintf(buffer+used-2,sizeof buffer-used+2,",\"callSlots\":[");
  for(uint32_t i=0;i<24;i++) {used=strlen(buffer);snprintf(buffer+used,sizeof buffer-used,"%s%u",i?",":"",slots[i]);}
  used=strlen(buffer);snprintf(buffer+used,sizeof buffer-used,"]}}");
 }
 return buffer;
}
static void rectangle(int x,int y,int w,int h,uint16_t colour,void *context) {
 (void)context;for(int j=y;j<y+h&&j<240;j++)for(int i=x;i<x+w&&i<240;i++)if(i>=0&&j>=0)screen[j*240+i]=colour;
}
const uint16_t *sw_host_screen(void) {if(panel_enabled) {if(sw_showbill_panel_visible(&showbill,&host,&panel))sw_panel_display(&host,&panel,rectangle,0);else sw_showbill_draw(&showbill,&host,rectangle,0);}else sw_display(&host,rectangle,0);return screen;}
const uint16_t *sw_host_bob_screen(uint32_t frame,uint32_t speaking) {
 rectangle(164,29,76,81,0x2a26,0);sw_display_bob(&host,frame,speaking,rectangle,0);return screen;
}
static void le16(FILE *f,uint32_t v) {fputc(v&255,f);fputc((v>>8)&255,f);}
static void le32(FILE *f,uint32_t v) {le16(f,v);le16(f,v>>16);}
int main(int argc,char **argv) {
 float buffer[256*2];uint32_t total=0;double energy=0;float peak=0;
 if(argc<2) {fprintf(stderr,"usage: bbswing output.wav [seed] [modern 0..100] [seconds; 0=full song]\n");return 2;}
 sw_init(&host,argc>2?(uint32_t)strtoul(argv[2],0,10):301026);
 if(argc>3)sw_command(&host,SW_MODERN,atoi(argv[3]));
 if(argc>3&&atoi(argv[3])>0)sw_command(&host,SW_BAND,255);
 sw_command(&host,SW_PLAY,0);
 uint32_t limit=argc>4&&atoi(argv[4])>0?(uint32_t)atoi(argv[4])*SW_RATE:(uint32_t)(host.song.count*4*60*SW_RATE/host.bpm+SW_RATE*2);
 FILE *f=fopen(argv[1],"wb");if(!f) {perror(argv[1]);return 1;}
 fwrite("RIFF",1,4,f);le32(f,36+limit*4);fwrite("WAVEfmt ",1,8,f);le32(f,16);le16(f,1);le16(f,2);le32(f,SW_RATE);le32(f,SW_RATE*4);le16(f,4);le16(f,16);fwrite("data",1,4,f);le32(f,limit*4);
 while(total<limit) {
  uint32_t n=limit-total>256?256:limit-total;
  if(total>=SW_RATE*12&&total<SW_RATE*12+256)sw_command(&host,SW_LEON,0);
  sw_render(&host,buffer,n);
  for(uint32_t i=0;i<n*2;i++) {float a=fabsf(buffer[i]);if(a>peak)peak=a;energy+=(double)buffer[i]*buffer[i];le16(f,(uint16_t)(int16_t)(buffer[i]*32767));}
  total+=n;
 }
 fclose(f);printf("%u frames; peak %.5f; RMS %.5f; engine %zu bytes; voice steals %u\n",total,peak,sqrt(energy/(total*2)),sizeof host,host.voice_steals);return 0;
}
