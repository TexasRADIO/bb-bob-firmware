/* Opt-in P0 r5 adapter. The legacy reference stays under the other branch. */
#ifdef SW_WITH_BASE_MACHINE
#ifndef SW_WITH_TIMBRE_BANK
#error The base-machine candidate requires the acoustic bank/cache build
#endif
#include "fm1_base_main.inc"
#else
/* SPDX-License-Identifier: GPL-3.0-only
 * BB-SWING platform: pinned Felucca hardware layer, original instrument above it.
 * No flash writes, USB commands, hidden autosave or hardware traffic from build tools. */
#include <stdint.h>
#include "fm1_time.h"
#include "fm1_sys.h"
#include "fm1_irq.h"
#include "fm1_guard.h"
#include "fm1_input.h"
#include "fm1_timer.h"
#include "fm1_audio.h"
#include "fm1_adc.h"
#include "fm1_lcd_hw.h"
#include "../vendor/fm1/src/libc.c"
#include "../vendor/fm1/src/lcd.c"
#include "swing.h"
#include "display.h"
#include "bob_identity.h"
#include "diagnostics.h"
#ifdef SW_WITH_TIMBRE_BANK
#include "timbre_bank.h"
#include "fm1_flash.h"
#endif
static sw_engine band;
static sw_panel panel;
static int32_t dma[SW_HALF_FRAMES*4] __attribute__((aligned(4)));
static float pcm[SW_HALF_FRAMES*2];
static uint16_t framebuffer[240*240] __attribute__((section(".pool"),aligned(4)));
static volatile uint32_t milliseconds;
static volatile sw_platform_stats stats;
#ifdef SW_DIAGNOSTICS
static sw_diagnostic_ui diagnostic_ui;
static uint32_t diagnostic_blit_begin;
#endif

extern uint32_t _data_start[],_data_end[],_data_load[],_bss_start[],_bss_end[];
extern uint32_t _pool_start[],_pool_end[],_rt_start[],_rt_end[],_rt_load[];
extern char _ustack_top[];
extern void isr_timer5(void),isr_alnk0(void);
volatile struct {uint32_t magic,failures,pending;} boot __attribute__((section(".noinit")));
static uint32_t critical_begin(void) {fm1_irq_off();return fm1_ticks();}
static void critical_end(uint32_t start) {sw_platform_mask(&stats,fm1_ticks()-start);fm1_irq_on();}
#ifdef SW_DIAGNOSTICS
/* Caller masks scan/audio. Only diagnostic counters change, never scan state. */
static void diagnostic_render_reset(void){
 sw_profile_reset(&band.profile);fm1_in_stat.cost_max=fm1_in_stat.gap_max=0;fm1_in_stat.last=fm1_ticks();
 for(uint32_t i=0;i<FM1_NCOL;i++)fm1_in_stat.on_max[i]=0;
}
static void diagnostic_lcd_poll(void){
 if(stats.lcd_pending&&(FM1_LCD_SPI_CON&0x8000u)){
  uint32_t now=fm1_ticks(),start=critical_begin();
  if(sw_diagnostics_lcd_done(&diagnostic_ui,&stats,milliseconds,now-diagnostic_blit_begin))diagnostic_render_reset();
  critical_end(start);
 }
}
#endif
static void paint_stack(uint32_t *low,uint32_t *high,uint32_t sp,uint32_t ssp) {
 uint32_t *end=high;
 uint32_t base=(uint32_t)(uintptr_t)low;
 if(sp>=base&&sp<=(uint32_t)(uintptr_t)high)end=(uint32_t *)(uintptr_t)(sp-base<64u?base:sp-64u);
 if(ssp>=base&&ssp<=(uint32_t)(uintptr_t)end)end=(uint32_t *)(uintptr_t)(ssp-base<64u?base:ssp-64u);
 for(uint32_t *p=low;p<end;p++)*p=SW_STACK_PAINT;
}
void fm1_timer5_irq(void) {
 static uint32_t last,acc;
 uint32_t now=fm1_ticks();
 #ifdef SW_DIAGNOSTICS
 uint32_t audio_before=stats.audio_total_ticks;
 #endif
 fm1_timer5_ack();fm1_input_tick();acc+=now-last;last=now;
 while(acc>=24000) {acc-=24000;milliseconds++;}
 #ifdef SW_DIAGNOSTICS
 uint32_t wall=fm1_ticks()-now,audio=stats.audio_total_ticks-audio_before;
 sw_platform_timer5(&stats,now,wall>=audio?wall-audio:wall);
 #endif
}
__attribute__((section(".text.ram_bridge"))) void fm1_alnk0_irq(void) {
 uint32_t start=fm1_ticks();
 uint8_t pending=fm1_audio_pending();fm1_audio_ack_aux(pending);
 if(pending&FM1_AUDIO_HALF) {
  uint32_t half=fm1_audio_free_half();
  #ifdef SW_DIAGNOSTICS
  uint32_t scan_begin=fm1_in_stat.cost_sum,render_begin=fm1_ticks();
  sw_render_profile before=band.profile;
  #endif
  sw_render(&band,pcm,SW_HALF_FRAMES);
  #ifdef SW_DIAGNOSTICS
  uint32_t render_end=fm1_ticks();
  #endif
  for(uint32_t i=0;i<SW_HALF_FRAMES*2;i++)dma[half*SW_HALF_FRAMES*2+i]=(int32_t)(pcm[i]*4194303.f);
  #ifdef SW_DIAGNOSTICS
  sw_diagnostics_work(&stats,fm1_in_stat.cost_sum-scan_begin,render_end-render_begin,fm1_ticks()-render_end,&before,&band.profile);
  #endif
  uint32_t missed=fm1_audio_free_half()!=half;fm1_audio_ack_half();
  sw_platform_half(&stats,start,fm1_ticks()-start,missed);
 }
}
static void rectangle(int x,int y,int w,int h,uint16_t colour,void *context) {
 (void)context;uint16_t swapped=(uint16_t)((colour>>8)|(colour<<8));
 for(int j=y;j<y+h;j++)for(int i=x;i<x+w;i++)framebuffer[j*240+i]=swapped;
}
static void draw(void) {
 #ifdef SW_DIAGNOSTICS
 diagnostic_lcd_poll();
 uint32_t timeout_before=fm1_lcd_timeouts;
 #endif
 lcd_sync();
 #ifdef SW_DIAGNOSTICS
 /* A delayed foreground poll may reach the next draw before observing DONE.
    lcd_sync already waited in the original path; count that completion only
    if the HAL did not time out. This remains an observed upper bound. */
 if(stats.lcd_pending&&fm1_lcd_timeouts==timeout_before){
  uint32_t now=fm1_ticks(),start=critical_begin();
  if(sw_diagnostics_lcd_done(&diagnostic_ui,&stats,milliseconds,now-diagnostic_blit_begin))diagnostic_render_reset();
  critical_end(start);
 }
 uint32_t draw_begin=fm1_ticks();
 #endif
 if((fm1_in.buttons&SW_DIAGNOSTIC_BUTTONS)==SW_DIAGNOSTIC_BUTTONS) {
  #ifdef SW_DIAGNOSTICS
  uint32_t start=critical_begin();
  stats.scan_tick_peak=fm1_in_stat.cost_max;stats.scan_gap_peak=fm1_in_stat.gap_max;
  for(uint32_t i=0;i<FM1_NCOL;i++)sw_platform_peak(&stats.scan_on_peak,fm1_in_stat.on_max[i]);
  sw_platform_stats view=diagnostic_ui.captured?diagnostic_ui.snapshot:stats;critical_end(start);
  sw_diagnostics_display(&view,diagnostic_ui.page,diagnostic_ui.captured,rectangle,0);
  #else
  uint32_t start=critical_begin();sw_platform_stats view=stats;critical_end(start);
  sw_platform_display(&view,rectangle,0);
  #endif
 } else sw_panel_display(&band,&panel,rectangle,0);
 #ifdef SW_DIAGNOSTICS
 diagnostic_blit_begin=fm1_ticks();sw_platform_peak(&stats.draw_peak,diagnostic_blit_begin-draw_begin);
 #endif
 lcd_blit(0,0,240,240,framebuffer);
 #ifdef SW_DIAGNOSTICS
 stats.lcd_pending=1;
 #endif
}
static void fm1_fault(const fm1_crash_t *crash) {
 (void)crash;fm1_audio_stop();lcd_fill(0,0,240,240,0xc800);
 fm1_delay_ms(1500);fm1_enter_uboot();
}
static void command(uint32_t id,int value) {uint32_t start=critical_begin();sw_command(&band,id,value);critical_end(start);}
static void input(void) {
 static uint32_t last_notes;
 uint32_t released=0,pressed=fm1_input_edges(&released);
 uint32_t button_levels=fm1_in.buttons;
 #ifdef SW_DIAGNOSTICS
 if((button_levels&SW_DIAGNOSTIC_BUTTONS)==SW_DIAGNOSTIC_BUTTONS) {
  uint32_t start=critical_begin(),run=stats.run;
  uint32_t consumed=sw_diagnostics_control(&diagnostic_ui,&stats,pressed,button_levels,milliseconds);
  band.profile.enabled=stats.profile_enabled;
  if(stats.run!=run) {
   diagnostic_render_reset();
  }
  critical_end(start);pressed&=~consumed;released&=~consumed;button_levels&=~consumed;
 }
 #endif
 /* Pop presses before reading current levels. A press arriving after this
    pop remains latched for the next poll instead of being mistaken for up. */
 uint32_t downs=fm1_input_note_edges(),notes=fm1_in.notes;
 uint32_t start=critical_begin();sw_panel_buttons_poll(&band,&panel,pressed,released,button_levels);critical_end(start);
 /* A complete short tap can be latched in downs while levels is already up.
    Route its press before its release instead of leaving a60second live gate. */
 uint32_t changes=downs|(last_notes&~notes);
 for(uint32_t i=0;i<27;i++)if(changes&(1u<<i)) {
  uint32_t bit=1u<<i;
  /* Preserve an IRQ opportunity between simultaneous note initialisations. */
  start=critical_begin();sw_panel_keys_poll(&band,&panel,notes&bit,downs&bit,last_notes&bit);critical_end(start);
 }
 last_notes=notes;
 for(uint32_t i=0;i<7;i++) {int steps=fm1_enc_take(i);if(steps) {start=critical_begin();sw_panel_encoder(&band,&panel,i,steps);critical_end(start);}}
 /* Only bounded actions run above with audio IRQ masked. Both original and
    recalled compositions are prepared here in the free pending buffer. */
 sw_panel_service(&band,&panel);
 if(band.classic_request&&!band.pending_ready) {
  uint32_t request=band.classic_request;band.classic_request=0;
  if(request==1)sw_prepare_classic(&band,band.classic_choice);else sw_prepare(&band,band.song.seed+1);
  if(!band.playing&&band.pending_ready)command(SW_MASTER,(int)(band.master_target*100.f+.5f));
 }
}
void fm1_cstart(void) {
 uint32_t *source,*dest;
 fm1_time_init();fm1_reset_reason();fm1_wdt_arm(0x0d);
 if(boot.magic!=0x53574242u) {boot.magic=0x53574242u;boot.failures=boot.pending=0;}
 if(boot.pending)boot.failures++;
 boot.pending=1;
 if(boot.failures>=2) {boot.failures=boot.pending=0;fm1_enter_uboot();}
 fm1_irq_init();
 for(dest=_bss_start;dest<_bss_end;dest++)*dest=0;
 for(dest=_pool_start;dest<_pool_end;dest++)*dest=0;
 for(source=_data_load,dest=_data_start;dest<_data_end;)*dest++=*source++;
 for(source=_rt_load,dest=_rt_start;dest<_rt_end;)*dest++=*source++;
 uint32_t sp,ssp;__asm__ volatile("%0 = sp" : "=r"(sp));__asm__ volatile("%0 = ssp" : "=r"(ssp));
 paint_stack((uint32_t *)_ustack_lo,(uint32_t *)_ustack_top,sp,ssp);
 paint_stack((uint32_t *)_sstack_lo,(uint32_t *)_sstack_top,sp,ssp);
 fm1_mailbox_clear();fm1_guard_enable(FM1_GUARD_STACK|FM1_GUARD_WRITE|FM1_GUARD_BUS|FM1_GUARD_PC);
 /* Hash the exact linked XIP image once, before audio/scan starts. The
    data initializer remains unchanged in flash; digest lives only in RAM. */
 uint32_t app_length=(uint32_t)(uintptr_t)_data_load+(uint32_t)((uintptr_t)_data_end-(uintptr_t)_data_start)-0x02000120u;
 sw_bob_hash_application((const unsigned char *)(uintptr_t)0x02000120u,app_length,fm1_wdt_feed);
 #ifdef SW_WITH_TIMBRE_BANK
 /* Read-only library slot. Do this before audio/scan and before publishing
    any sample pointers. No bank-write/updater action is performed here. */
 fl_plain_window_init();
 sw_timbre_bank_boot(fm1_xip_ptr(SW_TB_FLASH_OFFSET),SW_TB_SLOT_BYTES,fm1_wdt_feed);
 sw_period_timbres_boot(fm1_wdt_feed);
 #endif
 sw_init(&band,301026u);sw_panel_init(&panel);band.master=band.master_target=0;
 #ifdef SW_DIAGNOSTICS
 band.profile.clock=fm1_ticks;
 #endif
 if(sw_classic_ready_count()) {sw_command(&band,SW_CLASSIC_MODE,1);sw_prepare_classic(&band,band.classic_choice);band.classic_request=0;sw_command(&band,SW_MASTER,0);}
 lcd_init();draw();lcd_sync();
 fm1_input_init();fm1_adc_init();
 int pot=fm1_adc_read(FM1_ADC_MASTER);if(pot>=0)band.master=band.master_target=pot/1023.f;
 #ifdef SW_DIAGNOSTICS
 sw_platform_reset(&stats,milliseconds);sw_profile_reset(&band.profile);
 #endif
 fm1_audio_init(dma,SW_HALF_FRAMES*2,isr_alnk0,4);fm1_timer5_start(isr_timer5,3);
 fm1_guard_lock_top();fm1_irq_enable_all();
 uint32_t screen_time=0,adc_time=0,boot_time=milliseconds,update_time=0,stack_time=0;
 for(;;) {
  fm1_wdt_feed();
  #ifdef SW_DIAGNOSTICS
  diagnostic_lcd_poll();
  #endif
  input();
  if(milliseconds-adc_time>=25) {adc_time=milliseconds;pot=fm1_adc_read(FM1_ADC_MASTER);if(pot>=0)command(SW_MASTER,pot*100/1023);}
  if(milliseconds-screen_time>=100) {
   sw_visual_advance(&band,milliseconds-screen_time);
   sw_panel_tick(&panel,milliseconds-screen_time);
   sw_panel_observe(&band,&panel);
   screen_time=milliseconds;
   /* Tiny view snapshot: no copying engine/delay state with IRQs masked. */
   #ifdef SW_DIAGNOSTICS
   if(!stats.lcd_disabled||diagnostic_ui.force_draw){draw();diagnostic_ui.force_draw=0;}
   #else
   draw();
   #endif
   for(uint32_t i=0;i<41;i++)fm1_led_key(i,(int)sw_panel_led(&band,&panel,i));
  }
  if(milliseconds-stack_time>=1000) {
   stack_time=milliseconds;
   uint32_t used=sw_platform_stack_used((uint32_t *)_ustack_lo,(uint32_t *)_ustack_top);if(used>stats.user_stack)stats.user_stack=used;
   used=sw_platform_stack_used((uint32_t *)_sstack_lo,(uint32_t *)_sstack_top);if(used>stats.irq_stack)stats.irq_stack=used;
  }
  if(milliseconds-boot_time>30000)boot.pending=boot.failures=0;
  /* Deliberate physical recovery path; no write to the instrument's saved areas. */
  if((fm1_in.buttons&3u)!=3u)update_time=milliseconds;
  else if(milliseconds-update_time>=5000) {fm1_audio_stop();boot.pending=0;fm1_enter_uboot();}
 }
}

#endif /* SW_WITH_BASE_MACHINE */
