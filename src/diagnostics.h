/* SPDX-License-Identifier: GPL-3.0-only; offline-testable diagnostic controls/view. */
#ifndef SW_DIAGNOSTICS_H
#define SW_DIAGNOSTICS_H
#include "display.h"
#ifdef SW_DIAGNOSTICS
typedef struct {uint8_t page,force_draw,freeze_armed,captured;sw_platform_stats snapshot;} sw_diagnostic_ui;
/* Caller serializes this with scan/audio IRQs. Returns button bits consumed
   by the diagnostic chord so HOME cannot also change the song/title. */
uint32_t sw_diagnostics_control(sw_diagnostic_ui *ui,volatile sw_platform_stats *s,uint32_t pressed,uint32_t levels,uint32_t now);
void sw_diagnostics_display(const sw_platform_stats *s,uint32_t page,uint32_t captured,sw_rectangle rectangle,void *context);
/* Returns1 when draining the last frame starts a clean frozen workload. */
uint32_t sw_diagnostics_lcd_done(sw_diagnostic_ui *ui,volatile sw_platform_stats *s,uint32_t now,uint32_t elapsed);
void sw_diagnostics_work(volatile sw_platform_stats *s,uint32_t scan,uint32_t render,uint32_t convert,const sw_render_profile *before,const sw_render_profile *after);
#endif
#endif
