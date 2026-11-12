/* SPDX-License-Identifier: GPL-3.0-only */
#include "bb_bank_writer.h"

#define TIMBRE_BASE 0xA0000u
#define ART_BASE 0xE8000u
#define SECTOR 4096u
#define PAGE 256u

static uint32_t update(uint32_t crc, const uint8_t *p, uint32_t n)
{
    while (n--) {
        crc ^= *p++;
        for (uint32_t bit = 0; bit < 8u; bit++)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return crc;
}

uint32_t bb_bank_crc32(const void *data, uint32_t bytes)
{
    return ~update(~0u, (const uint8_t *)data, bytes);
}

static uint32_t word(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static int fail(bb_bank_writer *w)
{
    w->state = BB_BANK_FAILED;
    return 0;
}

static int header_ok(const bb_bank_writer *w)
{
    const uint8_t *p = w->header;
    if (bb_bank_crc32(p, w->header_bytes) != w->header_crc)
        return 0;
    if (w->restore)
        return 1;
    if (w->target == BB_BANK_TIMBRES)
        return word(p) == 0x42544242u && word(p + 4) == 1u &&
               word(p + 8) == 79712u && word(p + 16) == 4u && word(p + 20) == 79200u;
    static const uint8_t magic[8] = {'B','B','B','O','B','0','1',0};
    static const uint8_t profile[8] = {'B','B','A','R','T','0','1',0};
    for (uint32_t i = 0; i < 8u; i++)
        if (p[i] != magic[i] || p[40u + i] != profile[i])
            return 0;
    return word(p + 12) == BB_BANK_BYTES && word(p + 28) == 83u;
}

static int begin(bb_bank_writer *w, const bb_bank_io *io, uint32_t target,
                 uint32_t expected_crc, uint32_t header_crc, uint32_t restore)
{
    if (!w || !io || !io->read || !io->erase || !io->program || target > BB_BANK_ART)
        return 0;
    if (w->state == BB_BANK_RECEIVING)
        return w->target == target && w->expected_crc == expected_crc &&
               w->header_crc == header_crc && w->restore == restore;
    w->io = *io;
    w->state = BB_BANK_RECEIVING;
    w->target = target;
    w->base = target == BB_BANK_TIMBRES ? TIMBRE_BASE : ART_BASE;
    w->header_bytes = target == BB_BANK_TIMBRES ? 512u : 256u;
    w->expected_crc = expected_crc;
    w->header_crc = header_crc;
    w->restore = restore;
    w->next = 0;
    w->crc = ~0u;
    for (uint32_t i = 0; i < BB_BANK_MAX_HEADER; i++)
        w->header[i] = 0;
    return 1;
}

int bb_bank_begin(bb_bank_writer *w, const bb_bank_io *io, uint32_t target,
                  uint32_t expected_crc, uint32_t header_crc)
{
    return begin(w, io, target, expected_crc, header_crc, 0u);
}

int bb_bank_begin_restore(bb_bank_writer *w, const bb_bank_io *io, uint32_t target,
                          uint32_t expected_crc, uint32_t header_crc)
{
    return begin(w, io, target, expected_crc, header_crc, 1u);
}

int bb_bank_write(bb_bank_writer *w, uint32_t off, const void *data,
                  uint32_t n, uint32_t chunk_crc)
{
    uint8_t check[BB_BANK_CHUNK];
    const uint8_t *p = (const uint8_t *)data;
    if (!w || w->state != BB_BANK_RECEIVING || !p || !n || n > BB_BANK_CHUNK ||
        off % BB_BANK_CHUNK || off > BB_BANK_BYTES || n > BB_BANK_BYTES - off ||
        (n != BB_BANK_CHUNK && off + n != BB_BANK_BYTES) || bb_bank_crc32(p, n) != chunk_crc)
        return 0;
    if (off != w->next) {
        if (off + n != w->next)
            return 0;
        if (off < w->header_bytes) {
            for (uint32_t i = 0; i < n; i++)
                if (w->header[off + i] != p[i])
                    return 0;
            return 1;
        }
        if (w->io.read(w->io.context, w->base + off, check, n))
            return fail(w);
        for (uint32_t i = 0; i < n; i++)
            if (check[i] != p[i])
                return 0;
        return 1;
    }
    if (off < w->header_bytes) {
        for (uint32_t i = 0; i < n; i++)
            w->header[off + i] = p[i];
        if (off + n == w->header_bytes && !header_ok(w))
            return fail(w);
    } else {
        if (off == w->header_bytes || off % SECTOR == 0u) {
            if (w->io.erase(w->io.context, w->base + (off & ~(SECTOR - 1u))))
                return fail(w);
        }
        if (w->io.program(w->io.context, w->base + off, p, n) ||
            w->io.read(w->io.context, w->base + off, check, n))
            return fail(w);
        for (uint32_t i = 0; i < n; i++)
            if (check[i] != p[i])
                return fail(w);
    }
    w->crc = update(w->crc, p, n);
    w->next += n;
    if (w->io.poll)
        w->io.poll(w->io.context);
    return 1;
}

static int read_crc(bb_bank_writer *w, uint32_t *value)
{
    uint8_t block[256];
    uint32_t crc = update(~0u, w->header, w->header_bytes);
    for (uint32_t off = w->header_bytes; off < BB_BANK_BYTES; off += sizeof block) {
        uint32_t n = BB_BANK_BYTES - off < sizeof block ? BB_BANK_BYTES - off : sizeof block;
        if (w->io.read(w->io.context, w->base + off, block, n))
            return 0;
        crc = update(crc, block, n);
        if (w->io.poll)
            w->io.poll(w->io.context);
    }
    *value = ~crc;
    return 1;
}

int bb_bank_commit(bb_bank_writer *w)
{
    uint8_t check[PAGE];
    uint32_t crc;
    if (w && w->state == BB_BANK_COMMITTED)
        return 1;
    if (!w || w->state != BB_BANK_RECEIVING || w->next != BB_BANK_BYTES || ~w->crc != w->expected_crc)
        return 0;
    if (!read_crc(w, &crc) || crc != w->expected_crc)
        return fail(w);
    for (uint32_t off = w->header_bytes; off != 0u;) {
        off -= PAGE;
        if (w->io.program(w->io.context, w->base + off, w->header + off, PAGE) ||
            w->io.read(w->io.context, w->base + off, check, PAGE))
            return fail(w);
        for (uint32_t i = 0; i < PAGE; i++)
            if (check[i] != w->header[off + i])
                return fail(w);
    }
    if (!read_crc(w, &crc) || crc != w->expected_crc)
        return fail(w);
    w->state = BB_BANK_COMMITTED;
    return 1;
}

void bb_bank_abort(bb_bank_writer *w)
{
    if (w)
        w->state = BB_BANK_IDLE;
}
