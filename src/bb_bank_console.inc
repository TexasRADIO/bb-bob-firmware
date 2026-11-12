/* SPDX-License-Identifier: GPL-3.0-only
 * Minimal CDC ordinary-user bank transport. Included after usb.c and the
 * fixed-range flash hooks, so it cannot address arbitrary flash. */
#include "bb_bank_writer.h"

#define BB_CON_LINE 128u
static struct { char line[BB_CON_LINE]; uint32_t length; } bb_con;
static bb_bank_writer bb_writer;
static uint32_t bb_bank_hold, bb_bank_mask;
/* Set once during the normal boot path. These survive only until reset, but
 * unlike the transfer mask they describe what the runtime actually accepted
 * from flash after a complete reboot. */
static uint32_t bb_boot_timbre_status, bb_boot_art_status;

static void bb_con_putc(char c)
{
    if (!cdc.dtr || co_w - co_r >= CO_N)
        return;
    cdc_out[co_w % CO_N] = (uint8_t)c;
    RING_PUBLISH();
    co_w++;
}
static void bb_con_puts(const char *s) { while (*s) bb_con_putc(*s++); }
static void bb_con_dec(uint32_t v)
{
    char b[11]; uint32_t n = 0;
    do { b[n++] = (char)('0' + v % 10u); v /= 10u; } while (v && n < sizeof b);
    while (n) bb_con_putc(b[--n]);
}
static void bb_con_kv(const char *k, uint32_t v)
{
    bb_con_puts(k); bb_con_putc(' '); bb_con_dec(v); bb_con_puts("\r\n");
}
static int bb_con_word(const char **p, const char *w)
{
    const char *s = *p;
    while (*s == ' ') s++;
    while (*w && *s == *w) s++, w++;
    if (*w || (*s && *s != ' ')) return 0;
    *p = s; return 1;
}
static int bb_con_hex(const char **p, uint32_t *out)
{
    const char *s = *p; uint32_t v = 0, n = 0;
    while (*s == ' ') s++;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
    while (*s) {
        uint32_t d;
        if (*s >= '0' && *s <= '9') d = (uint32_t)(*s - '0');
        else if (*s >= 'a' && *s <= 'f') d = (uint32_t)(*s - 'a' + 10);
        else if (*s >= 'A' && *s <= 'F') d = (uint32_t)(*s - 'A' + 10);
        else break;
        if (n++ == 8u) return 0;
        v = (v << 4) | d; s++;
    }
    if (!n || (*s && *s != ' ')) return 0;
    *p = s; *out = v; return 1;
}
static int bb_con_end(const char *p) { while (*p == ' ') p++; return *p == 0; }

static int bb_bank_read(void *ctx, uint32_t off, void *out, uint32_t n)
{
    (void)ctx; fm1_wdt_feed(); return ota_fread(off, out, n);
}
static int bb_bank_erase(void *ctx, uint32_t off)
{
    uint32_t took; (void)ctx; fm1_wdt_feed(); return fl_erase4k(off, &took);
}
static int bb_bank_program(void *ctx, uint32_t off, const void *data, uint32_t n)
{
    (void)ctx; fm1_wdt_feed(); return fl_write(off, (const uint8_t *)data, n);
}
static void bb_bank_poll(void *ctx) { (void)ctx; fm1_wdt_feed(); }

static void bb_bank_reply(uint32_t rc)
{
    bb_con_kv("bank_rc", rc);
    bb_con_kv("bank_state", bb_writer.state);
    bb_con_kv("bank_target", bb_writer.target);
    bb_con_kv("bank_next", bb_writer.next);
    bb_con_kv("bank_held", bb_bank_hold);
    bb_con_kv("bank_mask", bb_bank_mask);
    bb_con_kv("bank_bytes", BB_BANK_BYTES);
    bb_con_kv("boot_timbre", bb_boot_timbre_status);
    bb_con_kv("boot_art", bb_boot_art_status);
}

static int bb_bank_exec(const char *p, void (*stop)(void), uint32_t playing, uint32_t master)
{
    uint32_t rc = 2u;
    if (bb_con_word(&p, "bbstat")) { if (bb_con_end(p)) rc = 0; bb_bank_reply(rc); return 1; }
    if (bb_con_word(&p, "bbid")) {
#ifdef SW_BOB_FULL_SHA
        if (!bb_con_end(p)) goto reply;
        /* The display digest is uppercase. The serial wire digest is lowercase. */
        for (uint32_t i = 0; i < 64u; ++i) {
            char c = BB_APP_SHA[i];
            if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F'))) goto reply;
        }
        bb_con_puts("app_sha256 ");
        for (uint32_t i = 0; i < 64u; ++i) {
            char c = BB_APP_SHA[i];
            bb_con_putc(c >= 'A' ? (char)(c + ('a' - 'A')) : c);
        }
        bb_con_puts("\r\n");
        return 1;
#else
        goto reply;
#endif
    }
    uint32_t restore = 0u;
    if (bb_con_word(&p, "bbrestore")) restore = 1u;
    else if (!bb_con_word(&p, "bbinit")) restore = 2u;
    if (restore < 2u) {
        uint32_t target, expected, header;
        if (!bb_con_hex(&p, &target) || !bb_con_hex(&p, &expected) || !bb_con_hex(&p, &header) || !bb_con_end(p))
            goto reply;
        if (!bb_flash_ok || playing || master || (!bb_mst.held && bb_boot_mode != BB_BOOT_SAFE)) goto reply;
        if (!bb_bank_hold) {
            if (stop) stop();
            fm1_audio_stop();
            bb_bank_hold = 1u;
            bb_bank_quiesce();
        }
        { const bb_bank_io io = {0, bb_bank_read, bb_bank_erase, bb_bank_program, bb_bank_poll};
          rc = (restore ? bb_bank_begin_restore(&bb_writer, &io, target, expected, header) :
                          bb_bank_begin(&bb_writer, &io, target, expected, header)) ? 0u : 3u; }
        goto reply;
    }
    if (bb_con_word(&p, "bbw")) {
        uint32_t off, crc, n = 0; uint8_t data[BB_BANK_CHUNK];
        if (!bb_bank_hold || !bb_con_hex(&p, &off) || !bb_con_hex(&p, &crc)) goto reply;
        while (*p == ' ') p++;
        while (*p) {
            uint32_t a, b; char x, y;
            if (n == sizeof data || !p[1]) goto reply;
            x = *p++; y = *p++;
            if (x >= '0' && x <= '9') a = (uint32_t)(x - '0'); else if (x >= 'a' && x <= 'f') a = (uint32_t)(x - 'a' + 10); else if (x >= 'A' && x <= 'F') a = (uint32_t)(x - 'A' + 10); else goto reply;
            if (y >= '0' && y <= '9') b = (uint32_t)(y - '0'); else if (y >= 'a' && y <= 'f') b = (uint32_t)(y - 'a' + 10); else if (y >= 'A' && y <= 'F') b = (uint32_t)(y - 'A' + 10); else goto reply;
            data[n++] = (uint8_t)((a << 4) | b);
        }
        rc = bb_bank_write(&bb_writer, off, data, n, crc) ? 0u : 3u;
        goto reply;
    }
    if (bb_con_word(&p, "bbcommit")) {
        if (!bb_bank_hold || !bb_con_end(p)) goto reply;
        if (bb_bank_commit(&bb_writer)) { bb_bank_mask |= 1u << bb_writer.target; rc = 0u; }
        else rc = 3u;
        goto reply;
    }
    if (bb_con_word(&p, "bbabort")) {
        if (!bb_con_end(p)) goto reply;
        bb_bank_abort(&bb_writer); rc = 0u; goto reply;
    }
    if (bb_con_word(&p, "bbrestart")) {
        if (!bb_con_end(p) || bb_bank_mask != 3u) goto reply;
        bb_bank_reply(0); fm1_delay_ms(30); bb_reset(0); return 1;
    }
    return 0;
reply:
    bb_bank_reply(rc); return 1;
}

static void bb_bank_console_service(void (*stop)(void), uint32_t playing, uint32_t master)
{
    while (ci_r != ci_w) {
        char c = (char)cdc_in[ci_r++ % CI_N];
        RING_PUBLISH();
        if (c == '\r' || c == '\n') {
            if (bb_con.length) {
                bb_con.line[bb_con.length] = 0;
                if (!bb_bank_exec(bb_con.line, stop, playing, master)) {
                    bb_con_puts("bank_rc 4\r\n");
                }
                bb_con.length = 0;
            }
        } else if (bb_con.length + 1u < sizeof bb_con.line) {
            bb_con.line[bb_con.length++] = c;
        } else {
            bb_con.length = 0;
        }
    }
}

static uint32_t bb_bank_writer_held(void) { return bb_bank_hold; }
static void bb_bank_boot_status(uint32_t timbre, uint32_t art)
{
    bb_boot_timbre_status = timbre;
    bb_boot_art_status = art;
}
