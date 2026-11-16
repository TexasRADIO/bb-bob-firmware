/* SPDX-License-Identifier: GPL-3.0-only
 * BB-SWING platform: pinned Felucca hardware layer, original instrument above it.
 * No hidden autosave or hardware traffic from build tools. Base machine (bb_base.h): USB MIDI always on,
 * the M-UPGRADE way home to BB1, remote STOP/telemetry, SAFE MODE instead of UBOOT. The only flash it can
 * write is the M-UPGRADE staging area, and only when the host starts an update. */
#include <stdint.h>
#include "fm1_time.h"
#include "fm1_sys.h"
#include "fm1_irq.h"
#include "fm1_xip.h"
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
#endif
#include "bb_base.h"
#include "showbill.h"
#include "art_bank.h"
static sw_engine band;
static sw_panel panel;
static sw_showbill showbill;
static sw_art_bank packed_art;
/* Exact immutable bank bytes in pool RAM. Drawing still uses the same reader
   and artwork; its data reads no longer compete for the XIP flash cache. */
static uint8_t art_mirror[SW_ART_BANK_BYTES] __attribute__((section(".pool"),aligned(4)));
static const uint8_t *art_mirror_load(void){
 const uint32_t *source=(const uint32_t *)fm1_xip_ptr(SW_ART_BANK_OFFSET);
 uint32_t *dest=(uint32_t *)(void *)art_mirror;
 for(uint32_t i=0;i<SW_ART_BANK_BYTES/4u;i++){
  dest[i]=source[i];
  if((i&1023u)==1023u)fm1_wdt_feed();
 }
 return art_mirror;
}
#if SW_HALF_FRAMES == 256u
/* Keep the existing fixed-RAM reserve. Pool is on-chip and already used by LCD DMA. */
static int32_t dma[SW_HALF_FRAMES*4] __attribute__((section(".pool"),aligned(4)));
static float pcm[SW_HALF_FRAMES*2] __attribute__((section(".pool"),aligned(4)));
#else
static int32_t dma[SW_HALF_FRAMES*4] __attribute__((aligned(4)));
static float pcm[SW_HALF_FRAMES*2];
#endif
static uint16_t framebuffer[240*240] __attribute__((section(".pool"),aligned(4)));
static volatile uint32_t milliseconds;
static volatile sw_platform_stats stats;
#ifdef SW_DIAGNOSTICS
static sw_diagnostic_ui diagnostic_ui;
static uint32_t diagnostic_blit_begin;
static uint32_t diagnostic_display_mode;
static uint32_t diagnostic_display_ready;
static const sb_art *diagnostic_full_art;
static void diagnostic_display_set(uint32_t mode);
#endif

extern uint32_t _data_start[],_data_end[],_data_load[],_bss_start[],_bss_end[];
extern uint32_t _pool_start[],_pool_end[],_rt_start[],_rt_end[],_rt_load[];
extern char _ustack_top[];
extern void isr_timer5(void),isr_alnk0(void);
static uint32_t critical_begin(void) {fm1_irq_off();return fm1_ticks();}
static void critical_end(uint32_t start) {sw_platform_mask(&stats,fm1_ticks()-start);fm1_irq_on();}
static void bb_bank_quiesce(void);
#ifdef SW_DIAGNOSTICS
static void diagnostic_render_reset(void);
#endif
#ifdef SW_PEAK_CONTEXT
#include "peak_context.h"
static bb_peak_state peak_state,peak_snapshot;
static volatile uint32_t peak_draw_active,peak_frozen;
static uint32_t peak_sequence,peak_snapshot_valid,peak_snapshot_epoch,peak_snapshot_ms,peak_snapshot_frozen;
static uint32_t peak_context_reply(uint8_t *out,uint32_t page);
static int peak_context_set(uint32_t value,uint32_t playing,uint32_t master);
static uint32_t peak_note_count(void){uint32_t n=0;for(uint32_t i=0;i<8;i++)n+=band.notes_started[i];return n;}
static void peak_record(bb_peak_record *r,uint32_t half,uint32_t flags,uint32_t notes,uint32_t acid){
 for(uint32_t i=0;i<BB_PEAK_WORDS;i++)r->w[i]=0;
 r->w[BB_PC_VALID]=1;r->w[BB_PC_HALF]=half;r->w[BB_PC_CLASSIC]=band.classic_mode?band.song.classic_id:0xffffffffu;
 r->w[BB_PC_SEED]=band.song.seed;r->w[BB_PC_BAR]=band.bar;r->w[BB_PC_TICK]=band.tick;
 r->w[BB_PC_SECTION]=band.bar<band.song.count?band.song.bars[band.bar].section:0xffffffffu;
 r->w[BB_PC_STYLE]=(uint32_t)(band.modern*100.f+.5f)|((uint32_t)(band.hiphop*100.f+.5f)<<8)|
  ((uint32_t)(band.modern_target*100.f+.5f)<<16)|((uint32_t)(band.hiphop_target*100.f+.5f)<<24);
 for(uint32_t i=0;i<SW_VOICES;i++)if(band.voice[i].active){r->w[BB_PC_VOICES]++;if(band.voice[i].player<8)r->w[BB_PC_FAMILIES]|=1u<<band.voice[i].player;}
 if(band.acid.active)r->w[BB_PC_FAMILIES]|=256u;
 r->w[BB_PC_NOTES]=notes;r->w[BB_PC_CALL]=band.cue_kind;r->w[BB_PC_CALL_POSITION]=band.cue_position;
 r->w[BB_PC_FLAGS]=flags;r->w[BB_PC_RECORD]=band.record_active;r->w[BB_PC_ACID_STARTS]=acid;
 r->w[BB_PC_BPM]=band.bpm;r->w[BB_PC_FRAMES]=band.rendered_frames;
}
#endif
#include "bb_base_hw.inc"
#include "editor.inc"
/* Detach every region-B view before the first bank erase. Bank hold remains
   latched through commit/abort, so no draw can use an erased or partial bank.
   An outstanding LCD DMA reads only framebuffer RAM; drain it before writing. */
static void bb_bank_quiesce(void){
 sw_showbill_set_art(0);
 #ifdef SW_DIAGNOSTICS
 diagnostic_full_art=0;
 #endif
 lcd_sync();
 memset(&packed_art,0,sizeof packed_art);
 #ifdef SW_DIAGNOSTICS
 stats.lcd_pending=0;stats.lcd_disabled=1;
 diagnostic_ui.force_draw=diagnostic_ui.freeze_armed=0;
 diagnostic_display_ready=0;
 #endif
}
#ifdef SW_DIAGNOSTICS
/* Volatile measurement state only.  The normal path restores the exact art
   pointer opened at boot; no-art exercises the existing bounded fallback;
   frozen suppresses future draws after any already-queued DMA has drained. */
static void diagnostic_display_set(uint32_t mode){
 if(bb_bank_writer_held())return;
 diagnostic_display_mode=mode;
 sw_showbill_set_art(mode==BB_PROFILE_DISPLAY_NO_ART?0:diagnostic_full_art);
 diagnostic_ui.captured=0;
 if(mode==BB_PROFILE_DISPLAY_FROZEN){
  stats.lcd_disabled=1;diagnostic_ui.force_draw=0;diagnostic_ui.freeze_armed=stats.lcd_pending?1u:0u;
  diagnostic_display_ready=stats.lcd_pending?0u:1u;
 }else{
  stats.lcd_disabled=0;diagnostic_ui.freeze_armed=0;diagnostic_ui.force_draw=1;
  diagnostic_display_ready=0;
 }
}
/* Caller masks scan/audio. Only diagnostic counters change, never scan state. */
static void diagnostic_render_reset(void){
 sw_profile_reset(&band.profile);fm1_in_stat.cost_max=fm1_in_stat.gap_max=0;fm1_in_stat.last=fm1_ticks();
 for(uint32_t i=0;i<FM1_NCOL;i++)fm1_in_stat.on_max[i]=0;
}
/* Complete a volatile display-mode transition only after its last relevant
   DMA has finished, then start the measured epoch. Caller masks scan/audio. */
static void diagnostic_lcd_complete(uint32_t now,uint32_t elapsed){
 uint32_t reset=sw_diagnostics_lcd_done(&diagnostic_ui,&stats,now,elapsed);
 if(diagnostic_display_mode==BB_PROFILE_DISPLAY_FROZEN){
  if(!stats.lcd_pending&&!diagnostic_ui.freeze_armed)diagnostic_display_ready=1;
 }else if(!diagnostic_display_ready&&!diagnostic_ui.force_draw&&!stats.lcd_pending){
  sw_platform_reset(&stats,now);diagnostic_display_ready=1;reset=1;
 }
 if(reset)diagnostic_render_reset();
}
static void diagnostic_lcd_poll(void){
 if(stats.lcd_pending&&(FM1_LCD_SPI_CON&0x8000u)){
  uint32_t now=fm1_ticks(),start=critical_begin();
  diagnostic_lcd_complete(milliseconds,now-diagnostic_blit_begin);
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
static volatile uint8_t in_audio;
void fm1_timer5_irq(void) {
 static uint32_t last,acc,sub,owed;
 uint32_t now=fm1_ticks();
 #ifdef SW_DIAGNOSTICS
 uint32_t audio_before=stats.audio_total_ticks;
 #endif
 fm1_timer5_ack();fm1_input_tick();acc+=now-last;last=now;
 while(acc>=24000) {acc-=24000;milliseconds++;}
 fm1_ms=milliseconds;
 /* USB: 2 kHz from this 10 kHz tick, all SIE traffic here. TIMER5 is below
    audio, so audio may preempt this poll; it can no longer delay audio entry. */
 if(++sub>=5u)sub=0;
 if(sub==0)owed|=1u;                         /* 2 kHz USB */
 if(sub==2)owed|=2u;                         /* 2 kHz TRS MIDI: <= ~7 bytes per call at 31250 baud */
 if(owed&&!in_audio) {if(owed&1u)usb_poll();if(owed&2u)uart_midi_poll();owed=0;}
 #ifdef SW_DIAGNOSTICS
 uint32_t wall=fm1_ticks()-now,audio=stats.audio_total_ticks-audio_before;
 sw_platform_timer5(&stats,now,wall>=audio?wall-audio:wall);
 #endif
}
__attribute__((section(".text.ram_bridge"))) void fm1_alnk0_irq(void) {
 uint32_t start=fm1_ticks();
 in_audio=1;
 uint8_t pending=fm1_audio_pending();fm1_audio_ack_aux(pending);
 if(pending&FM1_AUDIO_HALF) {
  uint32_t half=fm1_audio_free_half();
  #ifdef SW_DIAGNOSTICS
  uint32_t scan_begin=fm1_in_stat.cost_sum,render_begin=fm1_ticks();
  sw_render_profile before=band.profile;
  #endif
  #ifdef SW_PEAK_CONTEXT
  uint32_t context_playing=band.playing,notes_before=peak_note_count(),acid_before=band.acid_notes;
  uint32_t context_flags=(peak_draw_active?BB_PC_DRAW:0u)|
   ((lcd_busy&&!(FM1_LCD_SPI_CON&0x8000u))?BB_PC_DMA:0u)|
   (sw_call_active(&band)?BB_PC_CALL_ON:0u)|(band.record_active?BB_PC_RECORDED:0u)|(peak_frozen?BB_PC_FROZEN:0u);
  #endif
  sw_render(&band,pcm,SW_HALF_FRAMES);
  #ifdef SW_DIAGNOSTICS
  uint32_t render_end=fm1_ticks();
  #endif
  for(uint32_t i=0;i<SW_HALF_FRAMES*2;i++)dma[half*SW_HALF_FRAMES*2+i]=(int32_t)(pcm[i]*4194303.f);
  #ifdef SW_DIAGNOSTICS
  sw_diagnostics_work(&stats,fm1_in_stat.cost_sum-scan_begin,render_end-render_begin,fm1_ticks()-render_end,&before,&band.profile);
  #endif
  uint32_t missed=fm1_audio_free_half()!=half;
  #ifdef SW_PEAK_CONTEXT
  if(context_playing){
   uint32_t notes=peak_note_count()-notes_before,acid=band.acid_notes-acid_before;
   bb_peak_record record;record.w[BB_PC_VALID]=0;record.w[BB_PC_HALF]=stats.halves+1u;
   uint32_t elapsed=fm1_ticks()-start,budget=SW_HALF_TICKS;
   if(!peak_state.highest.w[BB_PC_VALID]||elapsed>peak_state.highest.w[BB_PC_TICKS]||elapsed>=budget||missed)
    peak_record(&record,stats.halves+1u,context_flags,notes,acid);
   elapsed=fm1_ticks()-start;
   if(!record.w[BB_PC_VALID]&&(elapsed>peak_state.highest.w[BB_PC_TICKS]||elapsed>=budget||missed))
    peak_record(&record,stats.halves+1u,context_flags,notes,acid);
   elapsed=fm1_ticks()-start;missed=fm1_audio_free_half()!=half;
   bb_peak_observe(&peak_state,&record,elapsed,missed,budget,bb_peak_mask(context_flags,notes+acid));
  }
  #endif
  missed=fm1_audio_free_half()!=half;fm1_audio_ack_half();
  sw_platform_half(&stats,start,fm1_ticks()-start,missed);
 }
 in_audio=0;
}
static void rectangle(int x,int y,int w,int h,uint16_t colour,void *context) {
 (void)context;uint16_t swapped=(uint16_t)((colour>>8)|(colour<<8));
 for(int j=y;j<y+h;j++)for(int i=x;i<x+w;i++)framebuffer[j*240+i]=swapped;
}
static void draw(void) {
 if(bb_bank_writer_held())return;
 #ifdef SW_PEAK_CONTEXT
 if(peak_frozen)return;
 peak_draw_active=1;
 #endif
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
  diagnostic_lcd_complete(milliseconds,now-diagnostic_blit_begin);
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
 } else if(sw_showbill_panel_visible(&showbill,&band,&panel)) {sw_showbill_on_screen(&showbill,0);sw_panel_display(&band,&panel,rectangle,0);}
 else {sw_showbill_on_screen(&showbill,1);sw_showbill_draw(&showbill,&band,rectangle,0);}
 #ifdef SW_DIAGNOSTICS
 diagnostic_blit_begin=fm1_ticks();sw_platform_peak(&stats.draw_peak,diagnostic_blit_begin-draw_begin);
 #endif
 lcd_blit(0,0,240,240,framebuffer);
 #ifdef SW_PEAK_CONTEXT
 peak_draw_active=0;
 #endif
 #ifdef SW_DIAGNOSTICS
 stats.lcd_pending=1;
 #endif
}
static void fm1_fault(const fm1_crash_t *crash) {
 (void)crash;fm1_audio_stop();lcd_fill(0,0,240,240,0xc800);
 fm1_delay_ms(1500);
 bb_boot_rec.safe_req=1;fm1_core_reset();    /* restart into SAFE MODE: USB + the way home, not the ROM */
}
static void command(uint32_t id,int value) {uint32_t start=critical_begin();sw_command(&band,id,value);critical_end(start);}
static void bb_band_stop(void) {if(band.playing)command(SW_PLAY,0);command(SW_MASTER,0);}
#ifdef SW_PEAK_CONTEXT
static uint32_t peak_context_reply(uint8_t *out,uint32_t page){
 uint32_t w[26],count=7;
 if(page>=BB_PEAK_PAGES)return bb_sx_ack(out,BB_CMD_CONTEXT,1);
 if(page==0){uint32_t mark=critical_begin();peak_snapshot=peak_state;peak_snapshot_epoch=bb_epoch;peak_snapshot_ms=bb_epoch_ms;peak_snapshot_frozen=peak_frozen;
  peak_sequence++;if(!peak_sequence)peak_sequence=1;peak_snapshot_valid=1;critical_end(mark);}
 else if(!peak_snapshot_valid||peak_snapshot_epoch!=bb_epoch)return bb_sx_ack(out,BB_CMD_CONTEXT,1);
 w[0]=1;w[1]=page;w[2]=peak_sequence;w[3]=peak_snapshot_epoch;w[4]=peak_snapshot_ms;w[5]=peak_snapshot.last_half;w[6]=peak_snapshot_frozen;
 if(page<2){const bb_peak_record *r=page?&peak_snapshot.highest:&peak_snapshot.first;
  for(uint32_t i=0;i<BB_PEAK_WORDS;i++)w[count++]=r->w[i];}
 else {uint32_t begin=(page-2u)*3u,end=begin+3u;if(end>BB_PEAK_CONDITIONS)end=BB_PEAK_CONDITIONS;
  for(uint32_t i=begin;i<end;i++){const bb_peak_condition *c=&peak_snapshot.condition[i];
   w[count++]=c->halves;w[count++]=c->over_budget;w[count++]=c->dma_misses;w[count++]=c->peak_ticks;w[count++]=c->total_low;w[count++]=c->total_high;}}
 return bb_sx_words(out,BB_CMD_CONTEXT,w,count);
}
static int peak_context_set(uint32_t value,uint32_t playing,uint32_t master){
 if(playing||master||bb_boot_mode!=BB_BOOT_NORMAL||bb_bank_writer_held())return 1;
 int fixed_preset=value>=16u&&value<=18u;
 int classic_slot=value>=32u&&value<=127u&&value-32u<sw_classic_slots();
 int grid_style=value>=20u&&value<=29u;
 if(fixed_preset||classic_slot||grid_style){
  if(band.pending_ready||band.classic_request||panel.request)return 1;
  /* These diagnostic selections can only run while stopped and muted.
     Style setters do not prepare a song or start transport. */
  if(grid_style){
   if(value<25u)command(SW_MODERN,(int)((value-20u)*25u));
   else command(SW_HIPHOP,(int)((value-25u)*25u));
   return 0;
  }
  /* Prepare in the foreground, as in input(). */
  uint32_t slot=fixed_preset?(value==16u?16u:2u):value-32u;
  if(!sw_classic_available(slot))return 1;
  command(SW_CLASSIC_MODE,1);command(SW_CLASSIC_SELECT,(int)slot);
  if(!sw_prepare_classic(&band,slot))return 1;
  band.classic_request=0;
  command(SW_MASTER,0);
  if(fixed_preset){
   command(SW_MODERN,value==16u?0:100);
   command(SW_HIPHOP,value==18u?100:0);
  }
  return 0;
 }
 if(value>2)return 1;
 if(value==2){bb_master_hold(&bb_mst,fm1_adc_read(FM1_ADC_MASTER));command(SW_MASTER,0);command(SW_PLAY,0);return 0;}
 uint32_t before=fm1_lcd_timeouts;lcd_sync();if(fm1_lcd_timeouts!=before)return 1;
 peak_frozen=value;peak_snapshot_valid=0;return 0;
}
#endif
static void input(void) {
 static uint32_t last_notes;
 uint32_t released=0,pressed=fm1_input_edges(&released);
 if(pressed|released)sw_showbill_touch(&showbill);
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
 /* Family lookup is foreground work; simultaneous page/key input sees the new map. */
 sw_panel_refresh_calls(&panel);
 /* A complete short tap can be latched in downs while levels is already up.
    Route its press before its release instead of leaving a60second live gate. */
 uint32_t changes=downs|(last_notes&~notes);
 if(changes)sw_showbill_touch(&showbill);
 for(uint32_t i=0;i<27;i++)if(changes&(1u<<i)) {
  uint32_t bit=1u<<i;
  /* Preserve an IRQ opportunity between simultaneous note initialisations. */
  start=critical_begin();sw_panel_keys_poll(&band,&panel,notes&bit,downs&bit,last_notes&bit);critical_end(start);
 }
 last_notes=notes;
 for(uint32_t i=0;i<7;i++) {int steps=fm1_enc_take(i);if(steps) {sw_showbill_touch(&showbill);start=critical_begin();sw_panel_encoder(&band,&panel,i,steps);critical_end(start);}}
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
 /* Decide in a local: bb_boot_mode lives in .bss, which the loop below clears (found by B5 on #80). */
 uint32_t mode=bb_boot_decide(&bb_boot_rec);
 if(mode==BB_BOOT_UBOOT)fm1_enter_uboot();   /* SAFE MODE failed twice: the ROM is the last resort */
 fm1_irq_init();
 for(dest=_bss_start;dest<_bss_end;dest++)*dest=0;
 for(dest=_pool_start;dest<_pool_end;dest++)*dest=0;
 for(source=_data_load,dest=_data_start;dest<_data_end;)*dest++=*source++;
 for(source=_rt_load,dest=_rt_start;dest<_rt_end;)*dest++=*source++;
 bb_boot_mode=mode;                          /* published only after .bss/.data/.ram_text are initialised */
 uint32_t sp,ssp;__asm__ volatile("%0 = sp" : "=r"(sp));__asm__ volatile("%0 = ssp" : "=r"(ssp));
 paint_stack((uint32_t *)_ustack_lo,(uint32_t *)_ustack_top,sp,ssp);
 paint_stack((uint32_t *)_sstack_lo,(uint32_t *)_sstack_top,sp,ssp);
 fm1_mailbox_clear();fm1_guard_enable(FM1_GUARD_STACK|FM1_GUARD_WRITE|FM1_GUARD_BUS|FM1_GUARD_PC);
 /* Hash the exact linked XIP image once, before audio/scan starts. The
    data initializer remains unchanged in flash; digest lives only in RAM. */
 uint32_t app_length=(uint32_t)(uintptr_t)_data_load+(uint32_t)((uintptr_t)_data_end-(uintptr_t)_data_start)-0x02000120u;
 sw_bob_hash_application((const unsigned char *)(uintptr_t)0x02000120u,app_length,fm1_wdt_feed);
 bb_flash_probe();
 if(bb_flash_ok)ota_boot_cleanup();          /* a staging area left by a return that never ran */
 if(bb_boot_mode==BB_BOOT_SAFE) {
  bb_selftest_init(&bb_test);
  lcd_init();fm1_input_init();fm1_adc_init();usb_start();uart_midi_init();fm1_timer5_start(isr_timer5,3);
  fm1_guard_lock_top();fm1_irq_enable_all();
  bb_safe_mode();
 }
 uint32_t boot_timbre_ready=0,boot_art_ready=0;
 #ifdef SW_WITH_TIMBRE_BANK
 fl_plain_window_init();
 sw_timbre_bank_boot(fm1_xip_ptr(SW_TB_FLASH_OFFSET),SW_TB_SLOT_BYTES,fm1_wdt_feed);
 sw_period_timbres_boot(fm1_wdt_feed);
 sw_call_tables_boot(fm1_wdt_feed);
 boot_timbre_ready=sw_timbre_bank_status()==SW_TB_READY;
 #endif
 sw_init(&band,301026u);sw_panel_init(&panel);sw_showbill_init(&showbill);sw_showbill_set_art(0);band.master=band.master_target=0;
 bob_editor_init();
 /* Boot foreground only, after SAFE decision and before audio/scan start. */
 if(sw_art_bank_open(&packed_art,art_mirror_load(),SW_ART_BANK_BYTES,fm1_wdt_feed)){sw_showbill_set_art(&packed_art.art);boot_art_ready=1;
  #ifdef SW_DIAGNOSTICS
  diagnostic_full_art=&packed_art.art;
  #endif
 }
 bb_bank_boot_status(boot_timbre_ready,boot_art_ready);
 #ifdef SW_DIAGNOSTICS
 band.profile.clock=fm1_ticks;
 #endif
 if(sw_classic_ready_count()) {sw_command(&band,SW_CLASSIC_MODE,1);sw_prepare_classic(&band,band.classic_choice);band.classic_request=0;sw_command(&band,SW_MASTER,0);}
 lcd_init();draw();lcd_sync();
 #ifdef SW_DIAGNOSTICS
 stats.lcd_pending=0;diagnostic_display_ready=1;
 #endif
 fm1_input_init();fm1_adc_init();
 int pot=fm1_adc_read(FM1_ADC_MASTER);if(pot>=0)band.master=band.master_target=pot/1023.f;
 #ifdef SW_DIAGNOSTICS
 sw_platform_reset(&stats,milliseconds);sw_profile_reset(&band.profile);
 #endif
 bb_selftest_init(&bb_test);
 /* Audio must outrank the 10 kHz scan/USB poll.  The former 3/4 ordering let
    TIMER5 hold off an audio half for almost one whole 2.9 ms period. */
 fm1_audio_init(dma,SW_HALF_FRAMES*2,isr_alnk0,4);usb_start();uart_midi_init();fm1_timer5_start(isr_timer5,3);
 fm1_guard_lock_top();fm1_irq_enable_all();
 uint32_t screen_time=0,adc_time=0,stack_time=0;
 bb_hold hold={0,0};
 for(;;) {
  fm1_wdt_feed();
  usb_retry(milliseconds);
  bob_editor_service();
  bb_service(bb_band_stop,band.playing,(uint32_t)(band.master_target*100.f+.5f));
  bb_bank_console_service(bb_band_stop,band.playing,(uint32_t)(band.master_target*100.f+.5f));
  #ifdef SW_DIAGNOSTICS
  diagnostic_lcd_poll();
  #endif
  if(!bb_bank_writer_held())input();
  /* Sampling the pot is not a user gesture. Resetting the presentation timers
     here on every 25 ms poll prevents the show from ever becoming visible.
     sw_panel_observe reports a meaningful level change without ADC chatter. */
  if(!bb_bank_writer_held()&&milliseconds-adc_time>=25) {adc_time=milliseconds;int level=bb_master_from_pot(&bb_mst,fm1_adc_read(FM1_ADC_MASTER));if(level>=0)command(SW_MASTER,level);}
  if(!bb_bank_writer_held()&&milliseconds-screen_time>=100) {
   sw_visual_advance(&band,milliseconds-screen_time);
   sw_panel_tick(&panel,milliseconds-screen_time);
   uint32_t panel_mark=critical_begin();sw_panel_observe(&band,&panel);critical_end(panel_mark);
   sw_showbill_tick(&showbill,&band,milliseconds-screen_time);
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
  bb_boot_healthy(&bb_boot_rec,milliseconds);
  /* OCT- + OCT+ held 5 s: RETURN MODE (SAFE MODE). No write to the instrument's saved areas. */
  if(bb_hold_step(&hold,(fm1_in.buttons&3u)==3u,milliseconds,BB_BOOT_NORMAL)==1u)bb_reset(1);
 }
}
