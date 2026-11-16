/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "panel.h"
#define main sw_host_cli_main
#include "../src/host.c"
#undef main
static uint8_t bank[81920];
static void button(unsigned id) {sw_host_panel_control(0,id,1);sw_host_panel_control(0,id,0);}
static uint32_t pixels(void) {
 const uint16_t *p=sw_host_screen();uint32_t h=2166136261u;
 for(unsigned i=0;i<57600;i++)h=(h^p[i])*16777619u;
 return h;
}
static unsigned half_frame;
static void step(void) {
 sw_host_panel_control(4,0,0);
 sw_panel_buttons_poll(&host,&panel,0,0,0);
 sw_panel_keys_poll(&host,&panel,0,0,0);
 float audio[1103*2];sw_host_render(audio,(half_frame++&1u)?1103:1102);sw_host_visual_advance(25);
}
int main(int argc,char **argv) {
 assert(argc==3);FILE *f=fopen(argv[1],"rb");assert(f);assert(fread(bank,1,sizeof bank,f)==sizeof bank);assert(fgetc(f)==EOF);fclose(f);
 assert(sw_host_art_bank(bank,sizeof bank));
 sw_host_reset(301026);sw_host_panel_enable(1);button(SWP_HOME_BUTTON);button(SWP_PLAY);
 for(unsigned i=0;i<360;i++)step();
 assert(host.playing&&packed_art.bank.bytes);assert(!sw_showbill_panel_visible(&showbill,&host,&panel));
 uint32_t before=pixels(),changed=0;
 for(unsigned i=0;i<80;i++){step();if(i%4==0)changed+=pixels()!=before;}
 assert(changed>1);
 button(SWP_FX);assert(sw_showbill_panel_visible(&showbill,&host,&panel));
 for(unsigned i=0;i<100;i++)step();assert(!sw_showbill_panel_visible(&showbill,&host,&panel));
 const uint16_t *p=sw_host_screen();f=fopen(argv[2],"wb");assert(f);fprintf(f,"P6\n240 240\n255\n");
 for(unsigned i=0;i<57600;i++){fputc(((p[i]>>11)&31)*255/31,f);fputc(((p[i]>>5)&63)*255/63,f);fputc((p[i]&31)*255/31,f);}fclose(f);
 unsigned checked=0;
 for(unsigned tune=0;tune<sw_classic_slots();tune++)if(sw_classic_available(tune)) {
  sw_host_reset(301026);sw_host_panel_enable(1);
  sw_host_command(SW_CLASSIC_SELECT,(int32_t)tune);sw_host_command(SW_CLASSIC_MODE,1);
  button(SWP_PLAY);
  for(unsigned i=0;i<360;i++)step();
  assert(host.playing&&host.song.classic_id==tune+1);
  assert(!sw_showbill_panel_visible(&showbill,&host,&panel));
  uint32_t first=pixels(),moving=0;for(unsigned i=0;i<80;i++){step();if(i%4==0)moving+=pixels()!=first;}if(!moving)fprintf(stderr,"No changing frames in tune %u: %s\n",tune,sw_classic_title(tune));assert(moving);checked++;
 }
 assert(checked==50);
 for(unsigned mode=0;mode<3;mode++) {
  sw_host_reset(301026);sw_host_panel_enable(1);
  sw_host_command(SW_MODERN,mode?100:0);sw_host_command(SW_HIPHOP,mode==2?100:0);button(SWP_PLAY);
  for(unsigned i=0;i<360;i++)step();
  assert(!sw_showbill_panel_visible(&showbill,&host,&panel));
  uint32_t first=pixels(),moving=0;for(unsigned i=0;i<80;i++){step();if(i%4==0)moving+=pixels()!=first;}if(!moving)fprintf(stderr,"No changing frames in style %u\n",mode);assert(moving);
 }
 printf("Animated show matrix passed: %u Classics plus original Then, Now and Funk\n",checked);
 bank[1234]^=1;assert(!sw_host_art_bank(bank,sizeof bank));assert(!sw_showbill_art());
 puts("Bank animation regression passed: released art CRC, 40Hz volume + idle input polling, changing rendered frames, cue handback, damaged-bank refusal");return 0;
}
