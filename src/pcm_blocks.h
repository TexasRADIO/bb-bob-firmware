/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef SW_PCM_BLOCKS_H
#define SW_PCM_BLOCKS_H
#include <stdint.h>
/* Lossless signed-byte PCM. Each block stores its minimum and the exact
 * unsigned differences using 0..8 bits. Offsets address the byte payload;
 * one final zero guard permits a bounded two-byte peek. The caller supplies
 * an in-range sample index and the encoder's fixed block shift. */
typedef struct {uint16_t offset;int8_t minimum;uint8_t bits;} sw_pcm_block;
typedef char sw_pcm_block_size_check[(sizeof(sw_pcm_block)==4)?1:-1];
static inline int32_t sw_pcm_block_at(const uint8_t *data,const sw_pcm_block *blocks,uint32_t index,uint32_t shift) {
 const sw_pcm_block *b=&blocks[index>>shift];
 if(!b->bits)return b->minimum;
 uint32_t at=(index&((1u<<shift)-1u))*b->bits;
 uint32_t byte=b->offset+(at>>3);
 uint32_t pair=(uint32_t)data[byte]|(uint32_t)data[byte+1u]<<8;
 return (int32_t)b->minimum+(int32_t)((pair>>(at&7u))&((1u<<b->bits)-1u));
}
#endif
