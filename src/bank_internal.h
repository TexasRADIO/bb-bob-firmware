/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_BOB_BANK_INTERNAL_H
#define BB_BOB_BANK_INTERNAL_H
#include "bank.h"
/* Envelope validation only. Private to the BBART01 reader: a successful view
 * has NOT passed its required asset/row/reference checks. Never publish it. */
int sw_bank_open_art_envelope(sw_bank *,const uint8_t *,uint32_t,uint32_t,void (*)(void));
#endif
