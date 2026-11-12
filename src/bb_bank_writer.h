/* SPDX-License-Identifier: GPL-3.0-only
 * Fixed-slot BB-BOB bank writer. The target adapter supplies the NOR hooks;
 * this module accepts only the 80 KiB timbre and packed-art slots. */
#ifndef BB_BOB_BANK_WRITER_H
#define BB_BOB_BANK_WRITER_H
#include <stdint.h>

#define BB_BANK_BYTES 81920u
#define BB_BANK_CHUNK 32u
#define BB_BANK_MAX_HEADER 512u
enum { BB_BANK_TIMBRES = 0, BB_BANK_ART = 1 };
enum { BB_BANK_IDLE = 0, BB_BANK_RECEIVING = 1, BB_BANK_FAILED = 2, BB_BANK_COMMITTED = 3 };

typedef struct {
    void *context;
    int (*read)(void *, uint32_t, void *, uint32_t);
    int (*erase)(void *, uint32_t);
    int (*program)(void *, uint32_t, const void *, uint32_t);
    void (*poll)(void *);
} bb_bank_io;

typedef struct {
    bb_bank_io io;
    uint32_t state, target, base, header_bytes, expected_crc, header_crc, restore;
    uint32_t next, crc;
    uint8_t header[BB_BANK_MAX_HEADER];
} bb_bank_writer;

uint32_t bb_bank_crc32(const void *data, uint32_t bytes);
int bb_bank_begin(bb_bank_writer *, const bb_bank_io *, uint32_t target,
                  uint32_t expected_crc, uint32_t header_crc);
/* Recovery-only variant: same two fixed slots and header-last publication,
 * but accepts the exact fresh pre-install bytes rather than a BB-BOB header. */
int bb_bank_begin_restore(bb_bank_writer *, const bb_bank_io *, uint32_t target,
                          uint32_t expected_crc, uint32_t header_crc);
int bb_bank_write(bb_bank_writer *, uint32_t offset, const void *data,
                  uint32_t bytes, uint32_t chunk_crc);
int bb_bank_commit(bb_bank_writer *);
void bb_bank_abort(bb_bank_writer *);
#endif
