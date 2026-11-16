/* SPDX-License-Identifier: GPL-3.0-only */
#include <stdint.h>
#include <assert.h>
#include <stdio.h>
#include "fm1_flash_id.h"
int main(void) {
 assert(FL_JEDEC_OK(0x856014u));assert(FL_JEDEC_OK(0xB36014u));
 const uint32_t unknown[]={0u,0xffffffu,0x856015u,0xb36015u,0x856114u,0xb36013u,0x123414u,0xffffffffu};
 for(unsigned i=0;i<sizeof unknown/sizeof unknown[0];i++)assert(!FL_JEDEC_OK(unknown[i]));
 puts("Flash ID policy passed: both known parts admitted; unknown parts refused");
}
