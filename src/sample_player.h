/* SPDX-License-Identifier: GPL-3.0-only; one bounded recorded-instrument voice. */
#ifndef SW_SAMPLE_PLAYER_H
#define SW_SAMPLE_PLAYER_H
#include "bank.h"
typedef struct {
 sw_bank_clip clip;
 uint32_t position,decoded,step,output_frame,release,fraction,fraction_step;
 float last_output,retrigger_tail;
 int32_t predictor,previous,index;
 uint16_t selected;
 uint8_t active;
} sw_sample_player;
void sw_sample_start(sw_sample_player *p,const sw_bank_clip *clip,uint32_t selected);
void sw_sample_stop(sw_sample_player *p);
float sw_sample_read(sw_sample_player *p);
#endif
