/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "panel.h"
#define main sw_host_cli_main
#include "../src/host.c"
#undef main
void sw_host_reset(uint32_t);
void sw_host_panel_enable(uint32_t);
void sw_host_panel_control(uint32_t,uint32_t,int32_t);
void sw_host_visual_advance(uint32_t);
const char *sw_host_state(void);
static void poll(unsigned milliseconds, int jitter) {
 for(unsigned i=0;i<milliseconds/25;i++) {
  sw_host_panel_control(4,0,60+(jitter&&(i&1)));
  sw_host_visual_advance(25);
 }
}
static void button(unsigned b) {
 sw_host_panel_control(0,b,1);sw_host_panel_control(0,b,0);
}
int main(void) {
 sw_host_reset(301026);sw_host_panel_enable(1);
 button(SWP_HOME_BUTTON);button(SWP_PLAY);
 poll(9000,0);
 assert(strstr(sw_host_state(),"\"showbillPanel\":0"));
 assert(strstr(sw_host_state(),"\"master\":60"));
 poll(9000,1);
 assert(strstr(sw_host_state(),"\"showbillPanel\":0"));
 button(SWP_FX);
 assert(strstr(sw_host_state(),"\"showbillPanel\":1"));
 poll(2500,0);
 assert(strstr(sw_host_state(),"\"showbillPanel\":0"));
 puts("Master polling regression passed: steady volume, ADC jitter, cue handback, and volume retained");
 return 0;
}
