/* SPDX-License-Identifier: GPL-3.0-only */
#include "diagnostics.h"
#include "bob_identity.h"
#ifdef SW_DIAGNOSTICS
#ifndef SW_RENDER_PROFILE
#error Diagnostics requires optional render probes
#endif
uint32_t sw_diagnostics_control(sw_diagnostic_ui *ui,volatile sw_platform_stats *s,uint32_t pressed,uint32_t levels,uint32_t now) {
 const uint32_t home=1u<<SWP_HOME_BUTTON,edit=1u<<SWP_EDIT,fx=1u<<SWP_FX,seq=1u<<SWP_SEQ;
 if((levels&SW_DIAGNOSTIC_BUTTONS)!=SW_DIAGNOSTIC_BUTTONS)return 0;
 if(pressed&edit){ui->page=(uint8_t)((ui->page+1u)%3u);if(!s->lcd_disabled)ui->force_draw=1;}
 if(pressed&fx)s->profile_enabled^=1u;
 if(pressed&seq){
  if(s->lcd_disabled){ui->snapshot=*s;if(ui->freeze_armed)ui->snapshot.frozen_halves=0;ui->captured=1;s->lcd_disabled=0;ui->freeze_armed=0;}
  else{s->lcd_disabled=1;ui->captured=0;ui->freeze_armed=1;}
  ui->force_draw=1;
 }
 if(pressed&(home|fx)){
  sw_platform_reset(s,now);ui->captured=0;
  if(!s->lcd_disabled)ui->force_draw=1;
  else if(s->lcd_pending)ui->freeze_armed=1;
 }
 return home|edit|fx|seq;
}
uint32_t sw_diagnostics_lcd_done(sw_diagnostic_ui *ui,volatile sw_platform_stats *s,uint32_t now,uint32_t elapsed){
 s->lcd_pending=0;sw_platform_peak(&s->blit_peak,elapsed);
 if(s->lcd_disabled&&ui->freeze_armed&&!ui->force_draw){ui->freeze_armed=0;sw_platform_reset(s,now);return 1;}
 return 0;
}
void sw_diagnostics_work(volatile sw_platform_stats *s,uint32_t scan,uint32_t render,uint32_t convert,const sw_render_profile *before,const sw_render_profile *after) {
 s->scan_total_ticks+=scan;
 sw_platform_peak(&s->scan_half_peak,scan);sw_platform_peak(&s->render_peak,render);sw_platform_peak(&s->convert_peak,convert);
 sw_platform_peak(&s->voices_peak,after->peak_voices);
 if(after->enabled||before->enabled) {
  for(uint32_t i=0;i<SW_PROFILE_SCOPES;i++) {
   uint32_t ticks=after->ticks[i]-before->ticks[i];
   s->profile_total[i]+=ticks;sw_platform_peak(&s->profile_peak[i],ticks);
  }
  sw_platform_peak(&s->control_peak,after->ticks[SW_PROFILE_CONTROL]-before->ticks[SW_PROFILE_CONTROL]);
  sw_platform_peak(&s->voice_peak,after->ticks[SW_PROFILE_VOICES]-before->ticks[SW_PROFILE_VOICES]);
  sw_platform_peak(&s->mix_peak,after->ticks[SW_PROFILE_MIX]-before->ticks[SW_PROFILE_MIX]);
  sw_platform_peak(&s->note_peak,after->ticks[SW_PROFILE_NOTE]-before->ticks[SW_PROFILE_NOTE]);
  sw_platform_peak(&s->starts_peak,after->note_starts-before->note_starts);
 }
}
static void number(char *out,uint32_t n) {char b[12];uint32_t i=0;do{b[i++]=(char)('0'+n%10);n/=10;}while(n);for(uint32_t j=0;j<i;j++)out[j]=b[i-j-1];out[i]=0;}
void sw_diagnostics_display(const sw_platform_stats *s,uint32_t page,uint32_t captured,sw_rectangle r,void *c) {
 const uint16_t cream=0xff78,teal=0x2a26,gold=0xe66e;
 const char *labels[9];uint32_t values[9];
 const char *const timing[]={"RUN ID","AUDIO HALF PEAK US","BUDGET US","OVER BUDGET","DMA HALF MISSES","ENTRY GAP PEAK US","MASK BODY PEAK US","PEAK VOICES","HALVES RENDERED"};
 const char *const work[]={"RUN ID","RENDER WALL PEAK US","SCAN IN HALF US","CONVERT WALL PEAK US","CONTROL WALL US","VOICES WALL US","MIX WALL US","NOTE START WALL US","NOTE STARTS PER HALF"};
 const char *const screen[]={"RUN ID","LCD DRAW WALL US","LCD DMA WALL US","SCAN BODY MAX US","SCAN GAP MAX US","LED COL ON MAX US","U BOOT USED OF24320","S BOOT USED OF7936","LCD FROZEN HALVES"};
 uint32_t timing_values[]={s->run,s->peak_ticks/24u,SW_HALF_TICKS/24u,s->over_budget,s->dma_misses,s->entry_peak_ticks/24u,s->mask_peak_ticks/24u,s->voices_peak,s->halves};
 uint32_t work_values[]={s->run,s->render_peak/24u,s->scan_half_peak/24u,s->convert_peak/24u,s->control_peak/24u,s->voice_peak/24u,s->mix_peak/24u,s->note_peak/24u,s->starts_peak};
 uint32_t screen_values[]={s->run,s->draw_peak/24u,s->blit_peak/24u,s->scan_tick_peak/24u,s->scan_gap_peak/24u,s->scan_on_peak/24u,s->user_stack,s->irq_stack,s->frozen_halves};
 for(uint32_t i=0;i<9;i++){labels[i]=page==1?work[i]:page==2?screen[i]:timing[i];values[i]=page==1?work_values[i]:page==2?screen_values[i]:timing_values[i];}
 r(0,0,240,240,teal,c);sw_text(r,c,8,8,"AUDIO CHECK",2,cream);
 sw_text(r,c,8,27,"APP",1,cream);sw_text(r,c,32,27,sw_bob_app_sha_prefix,1,gold);
 sw_text(r,c,104,27,captured?"CAPTURED":s->profile_enabled?"FULL PROFILE":"LIGHT COUNTERS",1,cream);
 for(uint32_t i=0;i<9;i++){char b[12];number(b,values[i]);sw_text(r,c,8,43+(int)i*18,labels[i],1,cream);sw_text(r,c,172,43+(int)i*18,b,1,gold);}
 sw_text(r,c,8,216,"HOME RESET EDIT PAGE",1,cream);
 sw_text(r,c,8,230,captured?"CAPTURED - HOME NEW RUN":s->lcd_disabled?"LCD FROZEN - SEQ CAPTURES":"FX PROFILE SEQ FREEZE LCD",1,cream);
}
#endif
