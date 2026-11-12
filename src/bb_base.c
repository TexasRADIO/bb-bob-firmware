/* SPDX-License-Identifier: GPL-3.0-only
 * BB-BOB base machine logic (see bb_base.h). No registers, no globals. */
#include "bb_base.h"

uint32_t bb_boot_decide(volatile bb_boot *b)
{
    if (b->magic != BB_BOOT_MAGIC) {
        b->failures = b->pending = b->safe_req = b->safe_fail = 0;
        b->mode = BB_BOOT_NORMAL;
        b->magic = BB_BOOT_MAGIC;
    }
    if (b->pending) {                       /* the last boot never became healthy: it failed */
        if (b->mode == BB_BOOT_SAFE)
            b->safe_fail++;
        else
            b->failures++;
    }
    b->pending = 1;
    if (b->safe_fail >= 2u) {               /* SAFE MODE itself will not stay up: hand over to the ROM */
        b->failures = b->safe_fail = b->safe_req = b->pending = 0;
        return b->mode = BB_BOOT_UBOOT;
    }
    if (b->safe_req || b->failures >= 2u) {
        b->failures = 0;
        b->safe_req = 1;                    /* stays set until SAFE MODE is left on purpose */
        return b->mode = BB_BOOT_SAFE;
    }
    return b->mode = BB_BOOT_NORMAL;
}

void bb_boot_healthy(volatile bb_boot *b, uint32_t uptime_ms)
{
    if (uptime_ms > BB_HEALTHY_MS && b->pending) {
        b->pending = 0;
        if (b->mode == BB_BOOT_SAFE)
            b->safe_fail = 0;
        else
            b->failures = 0;
    }
}

void bb_boot_request_safe(volatile bb_boot *b, uint32_t on)
{
    b->safe_req = on != 0;
    b->pending = 0;                         /* intentional reset: not a failed boot */
    b->failures = b->safe_fail = 0;
}

uint32_t bb_hold_step(bb_hold *h, uint32_t both_down, uint32_t now_ms, uint32_t mode)
{
    uint32_t t;
    if (!both_down) {
        t = now_ms - h->since;
        if (h->held && mode == BB_BOOT_SAFE && t >= BB_HOLD_LEAVE_MS && t < BB_HOLD_UBOOT_MS) {
            h->held = 0;
            return 1;                       /* SAFE MODE: let go after 3 s -> restart normally */
        }
        h->held = 0;
        return 0;
    }
    if (!h->held) {
        h->held = 1;
        h->since = now_ms;
        return 0;
    }
    t = now_ms - h->since;
    if (mode == BB_BOOT_SAFE) {
        if (t >= BB_HOLD_UBOOT_MS)
            return 2;
        return 0;                           /* SAFE MODE acts on release, so a long hold can pass 3 s */
    }
    return t >= BB_HOLD_SAFE_MS ? 1u : 0u;
}

uint32_t bb_hold_countdown(const bb_hold *h, uint32_t now_ms, uint32_t mode)
{
    uint32_t t, limit = mode == BB_BOOT_SAFE ? BB_HOLD_UBOOT_MS : BB_HOLD_SAFE_MS;
    if (!h->held)
        return 0;
    t = now_ms - h->since;
    if (mode == BB_BOOT_SAFE && t >= BB_HOLD_LEAVE_MS && t < 10000u)
        return 0;                           /* "LET GO TO PLAY" shows instead (device glue) */
    if (t < 2000u || t >= limit)
        return 0;
    return (limit - t + 999u) / 1000u;
}

void bb_master_hold(bb_master *m, int32_t pot)
{
    m->held = 1;
    m->pot_at_hold = pot;
}

void bb_master_release(bb_master *m) { m->held = 0; }

int32_t bb_master_from_pot(bb_master *m, int32_t pot)
{
    int32_t d;
    if (pot < 0)
        return -1;                          /* no reading */
    if (m->held) {
        if (m->pot_at_hold < 0) {
            m->pot_at_hold = pot;           /* first reading after a hold made without one */
            return 0;
        }
        d = pot - m->pot_at_hold;
        if (d < BB_POT_WAKE && d > -BB_POT_WAKE)
            return 0;                       /* held at zero */
        m->held = 0;                        /* the player turned the knob: theirs again */
    }
    return pot * 100 / 1023;
}

uint32_t bb_sx_command(const uint8_t *f, uint32_t n)
{
    if (n < 4u || f[0] != BB_SX_ID0 || f[1] != BB_SX_ID1 || f[2] != BB_SX_ID2)
        return 0;
    if (f[3] < BB_CMD_IDENTITY || f[3] > BB_CMD_CONTEXT_SET)
        return 0;
    return f[3];
}

static uint32_t head(uint8_t *o, uint32_t cmd)
{
    o[0] = 0xF0;
    o[1] = BB_SX_ID0;
    o[2] = BB_SX_ID1;
    o[3] = BB_SX_ID2;
    o[4] = (uint8_t)cmd;
    return 5;
}

uint32_t bb_sx_ack(uint8_t *o, uint32_t cmd, uint32_t refused)
{
    uint32_t n = head(o, BB_CMD_ACK);
    o[n++] = (uint8_t)(cmd & 0x7Fu);
    o[n++] = refused ? 1 : 0;
    o[n++] = 0xF7;
    return n;
}

uint32_t bb_sx_identity(uint8_t *o, const char *id)
{
    uint32_t n = head(o, BB_CMD_IDENTITY);
    while (*id && n < 150u)
        o[n++] = (uint8_t)(*id++ & 0x7F);
    o[n++] = 0xF7;
    return n;
}

uint32_t bb_pack7(const uint8_t *in, uint32_t n, uint8_t *out)
{
    uint32_t acc = 0, nb = 0, o = 0;
    while (n--) {
        acc |= (uint32_t)*in++ << nb;
        nb += 8;
        while (nb >= 7u) {
            out[o++] = (uint8_t)(acc & 0x7Fu);
            acc >>= 7;
            nb -= 7;
        }
    }
    if (nb)
        out[o++] = (uint8_t)(acc & 0x7Fu);
    return o;
}

uint32_t bb_sx_words(uint8_t *o, uint32_t cmd, const uint32_t *w, uint32_t count)
{
    uint8_t raw[26u * 4u];
    uint32_t i, n = head(o, cmd);
    if (count > 26u)
        count = 26u;
    for (i = 0; i < count; i++) {
        raw[4 * i] = (uint8_t)w[i];
        raw[4 * i + 1] = (uint8_t)(w[i] >> 8);
        raw[4 * i + 2] = (uint8_t)(w[i] >> 16);
        raw[4 * i + 3] = (uint8_t)(w[i] >> 24);
    }
    n += bb_pack7(raw, count * 4u, o + n);
    o[n++] = 0xF7;
    return n;
}

uint32_t bb_sx_telemetry(uint8_t *o, const uint32_t *w) { return bb_sx_words(o, BB_CMD_TELEMETRY, w, BB_TELEMETRY_WORDS); }

void bb_selftest_init(bb_selftest *t)
{
    t->keys_seen = t->buttons_seen = t->enc_seen = t->keys_now = t->buttons_now = 0;
    t->pot_min = t->pot_max = t->pot_now = -1;
    t->midi_usb = t->midi_trs = t->midi_drops = 0;
}

void bb_selftest_inputs(bb_selftest *t, uint32_t keys, uint32_t buttons, uint32_t enc_moved, int32_t pot)
{
    keys &= (1u << BB_KEYS) - 1u;
    buttons &= (1u << BB_BUTTONS) - 1u;
    t->keys_seen |= keys ^ t->keys_now;           /* a change either way proves the switch */
    t->buttons_seen |= buttons ^ t->buttons_now;
    t->keys_now = keys;
    t->buttons_now = buttons;
    t->enc_seen |= enc_moved & ((1u << BB_ENCODERS) - 1u);
    if (pot >= 0) {
        t->pot_now = pot;
        if (t->pot_min < 0 || pot < t->pot_min)
            t->pot_min = pot;
        if (pot > t->pot_max)
            t->pot_max = pot;
    }
}

void bb_selftest_midi(bb_selftest *t, uint32_t source)
{
    if (source == 1u)
        t->midi_usb++;
    else if (source == 2u)
        t->midi_trs++;
}

uint32_t bb_bits(uint32_t v)
{
    uint32_t n = 0;
    while (v) {
        v &= v - 1u;
        n++;
    }
    return n;
}

uint32_t bb_selftest_pot_travel(const bb_selftest *t)
{
    if (t->pot_min < 0)
        return 0;
    return (uint32_t)(t->pot_max - t->pot_min) * 100u / 1023u;
}

uint32_t bb_sx_selftest(uint8_t *o, const bb_selftest *t)
{
    uint32_t w[10] = {t->keys_seen, t->buttons_seen, t->enc_seen, t->keys_now, t->buttons_now,
                      (uint32_t)t->pot_min, (uint32_t)t->pot_max, t->midi_usb, t->midi_trs, t->midi_drops};
    return bb_sx_words(o, BB_CMD_SELFTEST, w, 10);
}
