/* SPDX-License-Identifier: GPL-3.0-only
 * BB-BOB base machine, device side (included once by fm1_main.c, after the HAL headers).
 * Links the pinned Felucca USB-MIDI driver and owned hardened M-UPGRADE entry (src/bb_ota.inc),
 * restricts flash writes to the M-UPGRADE staging area, and supplies the hooks ota.c needs. */
#define FELUCCA_CDC 1                    /* CDC bank installer plus MIDI updater */
#define FELUCCA_CDC_DEFAULT 1
#define FELUCCA_UAC 0                    /* no USB audio input (bytes; a later option) */
#define FELUCCA_OTA 1                    /* M-UPGRADE entry: the way home to BB1 */
#define FELUCCA_OTA_DRYRUN 0
/* Identity answered to the updater handshake. The running application and the
 * package marker must use the same generated BB-BOB product identity. */
#define BB_PACKAGE_ID SW_BOB_PRODUCT_ID
#define FELUCCA_ID BB_PACKAGE_ID
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")
/* Writes are confined to M-UPGRADE staging plus the two fixed, host-held bank slots.
 * ota_in_area below remains staging-only; the bank console applies its own stopped,
 * master-zero, held-session protocol before either bank range is reachable. */
#define FL_RANGE_OK(off, n) (FL_IN(off, n, FL_OTA_LO, FL_OTA_HI) || \
                             FL_IN(off, n, 0xA0000u, 0xB4000u) || \
                             FL_IN(off, n, 0xE8000u, 0xFC000u))
#include "fm1_flash.h"
#include "../vendor/fm1/src/usb.c"
#include "../vendor/fm1/src/midi_uart.c"   /* TRS MIDI IN: UART1 RX DMA, polled with USB from TIMER5 */

/* ---- MIDI in: always drained, so a host or a TRS sender is never stalled ---- */
static bb_selftest bb_test;
static void bb_midi_drain(void)
{
    while (mi_r != mi_w) {
        RING_PUBLISH();                  /* read the slot only after seeing the index */
        bb_selftest_midi(&bb_test, midi_in_source[mi_r % MQ]);
        mi_r++;                          /* BB-BOB plays no MIDI notes yet: counted, then released */
    }
    if (midi_in_overflow) {
        bb_test.midi_drops++;
        midi_in_overflow = 0;
    }
}

static uint8_t bb_flash_ok;
/* ---- ota.c hooks (the SysEx frame hooks are in usb.c) ---- */
static uint32_t ota_now_ms(void) { return milliseconds; }
static void ota_idle(void) { fm1_wdt_feed(); }
static int ota_in_area(uint32_t off, uint32_t n) { return FL_IN(off, n, FL_OTA_LO, FL_OTA_HI); }   /* == ota.c OTA_AREA */
static int ota_erase(uint32_t off)
{
    uint32_t took;
    if (!bb_flash_ok || !ota_in_area(off, 0x1000u) || (off & 0xFFFu))
        return -8;
    fm1_audio_stop();                    /* a 4 KiB erase holds the flash (and so XIP) for tens of ms */
    return fl_erase4k(off, &took);
}
static int ota_prog(uint32_t off, const void *p, uint32_t n)
{
    if (!bb_flash_ok || !ota_in_area(off, n))
        return -8;
    return fl_write(off, p, n);
}
static int ota_fread(uint32_t off, void *p, uint32_t n)
{
    uint32_t f;
    int rc;
    if (!bb_flash_ok)
        return -8;
    f = irq_save();
    rc = FL_FAR(fl_read_ram)(off, p, n);
    irq_restore(f);
    return rc;
}
static void bb_show(const char *line1, const char *line2, uint16_t ink);

static volatile bb_boot bb_boot_rec __attribute__((section(".noinit")));
static void ota_commit(const uint8_t *parm)
{
    bb_boot_rec.pending = 0;             /* intentional reset */
    usb_detach();
    fm1_delay_ms(30);
    fm1_enter_update(parm);              /* record into RAM, core reset (fm1_sys.h): the SPL runs the loader */
}
#include "bb_ota.inc"

static void ota_show(uint32_t step, int32_t code)
{
    static const char *const STEP[] = {"", "PACKAGE", "CHECK HEAD", "LOADER", "CONFIRM", "RESTART"};
    if (step < 9u) {
        bb_show("RETURNING TO BB1", STEP[step < 6u ? step : 0], 0xff78u);
        return;
    }
    bb_show("RETURN STOPPED", code == OTA_ERR_CLEANUP ? "UPDATE CLEANUP FAILED" : code == 1 ? "DRY RUN OK" : "APP UNCHANGED", 0xb189u);
    fm1_delay_ms(1500);
}

/* ---- shared by normal play and SAFE MODE ---- */
static bb_master bb_mst;
static uint32_t bb_epoch, bb_epoch_ms;   /* measurement window: RESET bumps the epoch */
#ifdef SW_BOB_FULL_SHA
extern char sw_bob_app_sha_full[65];
#define BB_APP_SHA sw_bob_app_sha_full
#else
#define BB_APP_SHA sw_bob_app_sha_prefix
#endif
static uint32_t bb_boot_mode;

static void bb_flash_probe(void)
{
    uint32_t f = irq_save();
    bb_flash_ok = FL_FAR(fl_jedec_ram)() == 0x856014u;   /* the expected 1 MiB part, else never write */
    irq_restore(f);
}

static void bb_reset(uint32_t to_safe)
{
    fm1_audio_stop();
    bb_boot_request_safe(&bb_boot_rec, to_safe);
    usb_detach();
    fm1_delay_ms(30);
    fm1_core_reset();
}

#include "bb_bank_console.inc"

static void bb_uboot(void)               /* last resort only */
{
    fm1_audio_stop();
    bb_boot_rec.pending = 0;
    usb_detach();
    fm1_delay_ms(30);
    fm1_enter_uboot();
}

/* Fill the telemetry words from what this build knows; play/master come from the caller. */
static void bb_telemetry(uint32_t *w, uint32_t playing, uint32_t master)
{
    uint32_t start = critical_begin();
    w[BB_T_UPTIME_MS] = milliseconds;
    w[BB_T_HALVES] = stats.halves;
    w[BB_T_PEAK_TICKS] = stats.peak_ticks;
    w[BB_T_OVER_BUDGET] = stats.over_budget;
    w[BB_T_DMA_MISSES] = stats.dma_misses;
    w[BB_T_ENTRY_PEAK] = stats.entry_peak_ticks;
    w[BB_T_MASK_PEAK] = stats.mask_peak_ticks;
    w[BB_T_USER_STACK] = stats.user_stack;
    w[BB_T_IRQ_STACK] = stats.irq_stack;
    w[BB_T_USB_RESETS] = usb.resets;
    w[BB_T_USB_RX] = usb.rx_pkts;
    w[BB_T_USB_TX] = usb.tx_pkts;
    w[BB_T_USB_STATE] = (uint32_t)usb.up | (uint32_t)usb.config << 1 | (uint32_t)usb.suspended << 2;
    w[BB_T_BOOT] = bb_boot_mode | bb_boot_rec.failures << 8 | bb_boot_rec.safe_fail << 16 | (uint32_t)bb_flash_ok << 24;
    critical_end(start);
    w[BB_T_PLAYING] = playing;
    w[BB_T_MASTER] = master;
    w[BB_T_MIDI_USB_IN] = bb_test.midi_usb;
    w[BB_T_MIDI_TRS_IN] = bb_test.midi_trs;
    w[BB_T_MIDI_DROPS] = bb_test.midi_drops + um.drops;
    w[BB_T_EPOCH] = bb_epoch;
    w[BB_T_EPOCH_MS] = bb_epoch_ms;
}

#ifdef SW_DIAGNOSTICS
typedef struct {
    uint64_t total[SW_PROFILE_SCOPES];
    uint32_t peak[SW_PROFILE_SCOPES];
    uint32_t enabled, config, frames, note_starts, peak_voices;
    uint32_t timer5_body_peak, timer5_entry_peak;
} bb_profile_capture;
static bb_profile_capture bb_profile_snapshot;
static uint32_t bb_profile_snapshot_valid;

/* Five scopes per page keeps each reply below the protocol's 160-byte frame:
 * eleven header words, then total-low/total-high/per-half-peak for each scope.
 * Page zero atomically captures every scope; later pages use that same image. */
static uint32_t bb_profile_reply(uint8_t *out, uint32_t page)
{
    enum { PER_PAGE = 5u, HEADER_WORDS = 11u, SCHEMA = 3u };
    uint32_t w[26], first = page * PER_PAGE;
    if (first >= SW_PROFILE_SCOPES)
        return bb_sx_ack(out, BB_CMD_PROFILE, 1);
    uint32_t count = SW_PROFILE_SCOPES - first;
    if (count > PER_PAGE)
        count = PER_PAGE;
    uint32_t start = critical_begin();
    if (page == 0u) {
        bb_profile_snapshot.enabled = band.profile.enabled;
        bb_profile_snapshot.config = (band.profile.enabled ? BB_PROFILE_TIMERS : 0u) |
                                     (band.profile.force_synth ? BB_PROFILE_FORCE_SYNTH : 0u) |
                                     diagnostic_display_mode << BB_PROFILE_DISPLAY_SHIFT |
                                     (diagnostic_display_ready ? BB_PROFILE_DISPLAY_SETTLED : 0u);
        bb_profile_snapshot.frames = band.profile.frames;
        bb_profile_snapshot.note_starts = band.profile.note_starts;
        bb_profile_snapshot.peak_voices = band.profile.peak_voices;
        bb_profile_snapshot.timer5_body_peak = stats.timer5_body_peak;
        bb_profile_snapshot.timer5_entry_peak = stats.timer5_entry_peak;
        for (uint32_t i = 0; i < SW_PROFILE_SCOPES; i++) {
            bb_profile_snapshot.total[i] = stats.profile_total[i];
            bb_profile_snapshot.peak[i] = stats.profile_peak[i];
        }
        bb_profile_snapshot_valid = 1u;
    } else if (!bb_profile_snapshot_valid) {
        critical_end(start);
        return bb_sx_ack(out, BB_CMD_PROFILE, 1);
    }
    w[0] = SCHEMA;
    w[1] = page;
    w[2] = first;
    w[3] = count;
    w[4] = bb_profile_snapshot.enabled;
    w[5] = bb_profile_snapshot.config;
    w[6] = bb_profile_snapshot.frames;
    w[7] = bb_profile_snapshot.note_starts;
    w[8] = bb_profile_snapshot.peak_voices;
    w[9] = bb_profile_snapshot.timer5_body_peak;
    w[10] = bb_profile_snapshot.timer5_entry_peak;
    for (uint32_t i = 0; i < count; i++) {
        uint64_t total = bb_profile_snapshot.total[first + i];
        w[HEADER_WORDS + i * 3u] = (uint32_t)total;
        w[HEADER_WORDS + i * 3u + 1u] = (uint32_t)(total >> 32);
        w[HEADER_WORDS + i * 3u + 2u] = bb_profile_snapshot.peak[first + i];
    }
    critical_end(start);
    return bb_sx_words(out, BB_CMD_PROFILE, w, HEADER_WORDS + count * 3u);
}

static uint32_t bb_profile_set(uint32_t flags, uint32_t playing)
{
    /* A fallback change freezes/resumes sample clocks by design.  Requiring
     * STOP keeps paired measurements at a fresh, identical workload rather
     * than changing timbre part-way through live voices. */
    uint32_t display = (flags & BB_PROFILE_DISPLAY_MASK) >> BB_PROFILE_DISPLAY_SHIFT;
    if (playing || (flags & ~(BB_PROFILE_TIMERS | BB_PROFILE_FORCE_SYNTH | BB_PROFILE_DISPLAY_MASK)) ||
        display > BB_PROFILE_DISPLAY_FROZEN)
        return 1;
    uint32_t start = critical_begin();
    stats.profile_enabled = flags & BB_PROFILE_TIMERS;
    band.profile.enabled = stats.profile_enabled;
    band.profile.force_synth = (flags & BB_PROFILE_FORCE_SYNTH) != 0u;
    diagnostic_display_set(display);
    bb_profile_snapshot_valid = 0;
    sw_platform_reset(&stats, milliseconds);
    diagnostic_render_reset();
    bb_epoch++;
    bb_epoch_ms = milliseconds;
    critical_end(start);
    return 0;
}
#endif

/* One pass of the base machine's SysEx: our commands first, then the M-UPGRADE handshake and upgrade.
 * stop() stops the band and zeroes the master; returns nothing. Never blocks for long except ota_session,
 * which is the deliberate return. */
static void bb_service(void (*stop)(void), uint32_t playing, uint32_t master)
{
    static uint8_t out[160];
    const uint8_t *p;
    uint32_t n, cmd, len = 0, arg = 0, has_arg = 0;
    bb_midi_drain();
    bb_selftest_inputs(&bb_test, fm1_in.notes, fm1_in.buttons, 0, -1);
    if (ota_frame_get(&p, &n) && (cmd = bb_sx_command(p, n)) != 0) {
        if (n > 4u) { arg = p[4]; has_arg = 1; }
        ota_frame_done();
        if (cmd == BB_CMD_IDENTITY) {
            char id[140], *d = id;
            const char *parts[] = {SW_BOB_DISPLAY_VERSION, " ", SW_BOB_PRODUCT_ID, " APP ", BB_APP_SHA,
                                   bb_boot_mode == BB_BOOT_SAFE ? " SAFE" : " PLAY", " RET " BB_PACKAGE_ID};
            for (uint32_t i = 0; i < sizeof parts / sizeof *parts; i++)
                for (const char *s = parts[i]; *s && d < id + sizeof id - 1; s++)
                    *d++ = *s;
            *d = 0;
            len = bb_sx_identity(out, id);
        } else if (cmd == BB_CMD_TELEMETRY) {
            uint32_t w[BB_TELEMETRY_WORDS];
            bb_telemetry(w, playing, master);
            len = bb_sx_telemetry(out, w);
        } else if (cmd == BB_CMD_STOP) {
            if (stop)
                stop();
            bb_master_hold(&bb_mst, fm1_adc_read(FM1_ADC_MASTER));
            len = bb_sx_ack(out, cmd, 0);
        } else if (cmd == BB_CMD_RELEASE) {
            bb_master_release(&bb_mst);
            len = bb_sx_ack(out, cmd, 0);
        } else if (cmd == BB_CMD_RESET) {
            uint32_t start = critical_begin();   /* the audio ISR updates these: change them with it masked */
#ifdef SW_DIAGNOSTICS
            sw_platform_reset(&stats, milliseconds);
            diagnostic_render_reset();
            bb_profile_snapshot_valid = 0;
#else
            stats.halves = stats.peak_ticks = stats.over_budget = stats.dma_misses = 0;
            stats.entry_peak_ticks = stats.have_entry = stats.mask_peak_ticks = 0;   /* stack high-water kept */
#endif
#ifdef SW_PEAK_CONTEXT
            bb_peak_clear(&peak_state);peak_snapshot_valid=0;
#endif
            bb_epoch++;
            bb_epoch_ms = milliseconds;
            critical_end(start);
            len = bb_sx_ack(out, cmd, 0);
        } else if (cmd == BB_CMD_FAULT) {
            const volatile fm1_crash_t *c = &fm1_crash;
            uint32_t ok = c->magic == FM1_CRASH_MAGIC;
            uint32_t w[BB_FAULT_WORDS] = {ok, ok ? c->count : 0, c->vec, c->pc, c->rets, c->emu, c->dbg, c->sp, c->psr,
                                          c->uptime_ms, ok ? c->early : 0};
            len = bb_sx_words(out, BB_CMD_FAULT, w, BB_FAULT_WORDS);
        } else if (cmd == BB_CMD_PROFILE) {
#ifdef SW_DIAGNOSTICS
            len = has_arg ? bb_profile_reply(out, arg) : bb_sx_ack(out, cmd, 1);
#else
            len = bb_sx_ack(out, cmd, 1);
#endif
        } else if (cmd == BB_CMD_PROFILE_SET) {
#ifdef SW_DIAGNOSTICS
            len = bb_sx_ack(out, cmd, !has_arg || bb_profile_set(arg, playing));
#else
            len = bb_sx_ack(out, cmd, 1);
#endif
        } else if (cmd == BB_CMD_CONTEXT) {
#ifdef SW_PEAK_CONTEXT
            len=has_arg?peak_context_reply(out,arg):bb_sx_ack(out,cmd,1);
#else
            len=bb_sx_ack(out,cmd,1);
#endif
        } else if (cmd == BB_CMD_CONTEXT_SET) {
#ifdef SW_PEAK_CONTEXT
            if(has_arg&&arg==3&&!playing&&!master&&bb_boot_mode==BB_BOOT_NORMAL&&!bb_bank_writer_held()){
                len=bb_sx_ack(out,cmd,0);ota_wire_send(out,len);fm1_delay_ms(20);bb_reset(0);return;}
            len=bb_sx_ack(out,cmd,!has_arg||peak_context_set(arg,playing,master));
#else
            len=bb_sx_ack(out,cmd,1);
#endif
        } else if (cmd == BB_CMD_SELFTEST) {
            len = bb_sx_selftest(out, &bb_test);
        } else if (cmd == BB_CMD_SAFE) {
            len = bb_sx_ack(out, cmd, 0);
            ota_wire_send(out, len);
            fm1_delay_ms(20);
            bb_reset(bb_boot_mode != BB_BOOT_SAFE);
        } else
            len = bb_sx_ack(out, cmd, 0);
        if (len)
            ota_wire_send(out, len);
    }
    ota_service();                       /* M-UPGRADE handshake: our package identity */
    if (usb.ota_req) {                   /* M-UPGRADE upgrade command: the way home */
        usb.ota_req = 0;
        if (stop)
            stop();
        fm1_audio_stop();
        if (bb_flash_ok)
            ota_session();               /* returns only if nothing was committed */
    }
    if (usb.uboot_req)                   /* the host's stock soft key: honoured, as in Felucca */
        bb_uboot();
}

/* ---- SAFE MODE: audio off, engine untouched, USB + M-UPGRADE only ---- */
static void bb_text_rect(int x, int y, int w, int h, uint16_t c, void *ctx)
{
    (void)ctx;
    uint16_t s = (uint16_t)((c >> 8) | (c << 8));
    for (int j = y; j < y + h && j < 240; j++)
        for (int i = x; i < x + w && i < 240; i++)
            if (i >= 0 && j >= 0)
                framebuffer[j * 240 + i] = s;
}
static void bb_say(int y, const char *s, int scale, uint16_t ink)
{
    int n = 0;
    while (s[n])
        n++;
    sw_text(bb_text_rect, 0, 120 - (n * 6 * scale - scale) / 2, y, s, scale, ink);
}
static void bb_show(const char *line1, const char *line2, uint16_t ink)
{
    bb_text_rect(0, 0, 240, 240, 0x1144u, 0);
    bb_say(96, line1, 2, ink);
    if (line2 && *line2)
        bb_say(124, line2, 1, 0xff78u);
    lcd_sync();
    lcd_blit(0, 0, 240, 240, framebuffer);
    lcd_sync();
}

/* "<a><n>/<of><b><m>/<of2>"; of = 0 prints the count alone. Counts stay below 10^6. */
static char *bb_num(char *d, uint32_t v)
{
    char t[8];
    int k = 0;
    do {
        t[k++] = (char)('0' + v % 10u);
        v /= 10u;
    } while (v && k < 7);
    while (k)
        *d++ = t[--k];
    return d;
}
static void bb_fmt(char *d, const char *a, uint32_t n, uint32_t of, const char *b, uint32_t m, uint32_t of2)
{
    while (*a) *d++ = *a++;
    d = bb_num(d, n);
    if (of) { *d++ = '/'; d = bb_num(d, of); }
    while (*b) *d++ = *b++;
    d = bb_num(d, m);
    if (of2) { *d++ = '/'; d = bb_num(d, of2); }
    *d = 0;
}
static void bb_safe_draw(const bb_hold *h)
{
    char cd[24] = "UBOOT IN  ";
    uint32_t left = bb_hold_countdown(h, milliseconds, BB_BOOT_SAFE), t = milliseconds - h->since;
    bb_text_rect(0, 0, 240, 240, 0x1144u, 0);
    bb_say(36, "RETURN MODE", 2, 0xe66eu);
    bb_say(66, "SOUND OFF - NOTHING SAVED IS TOUCHED", 1, 0x8cebu);
    bb_say(96, usb.config ? (usb.suspended ? "USB: CABLE OR HOST ASLEEP" : "USB MIDI: CONNECTED") : "USB: PLUG IN THE CABLE", 1,
           usb.config && !usb.suspended ? 0xbed8u : 0xff78u);
    bb_say(114, "THE UPDATER CAN NOW INSTALL BB1", 1, 0xff78u);
    {   /* live I/O check: every control proves itself by changing; pressed keys light their own LEDs */
        char l1[40], l2[40];
        bb_fmt(l1, "KEYS ", bb_bits(bb_test.keys_seen), BB_KEYS, " BUTTONS ", bb_bits(bb_test.buttons_seen), BB_BUTTONS);
        bb_fmt(l2, "KNOBS ", bb_bits(bb_test.enc_seen), BB_ENCODERS, " VOLUME ", bb_selftest_pot_travel(&bb_test), 100);
        bb_say(136, l1, 1, 0xff78u);
        bb_say(148, l2, 1, 0xff78u);
        bb_fmt(l1, "MIDI USB ", bb_test.midi_usb, 0, " TRS ", bb_test.midi_trs, 0);
        bb_say(160, l1, 1, bb_test.midi_trs || bb_test.midi_usb ? 0xbed8u : 0x8cebu);
    }
    if (fm1_crash.magic == FM1_CRASH_MAGIC && fm1_crash.count) {
        static const char hx[] = "0123456789ABCDEF";
        char l[24] = "LAST CRASH PC ";
        for (uint32_t i = 0; i < 8u; i++)
            l[14 + i] = hx[(fm1_crash.pc >> (28u - 4u * i)) & 15u];
        l[22] = 0;
        bb_say(176, l, 1, 0xb189u);
    } else
        bb_say(176, SW_BOB_DISPLAY_VERSION, 1, 0x8cebu);
    if (h->held && t >= BB_HOLD_LEAVE_MS && t < 10000u)
        bb_say(196, "LET GO TO PLAY AGAIN", 1, 0xe66eu);
    else if (left && t >= 10000u) {
        cd[9] = (char)('0' + left);
        bb_say(196, cd, 1, 0xb189u);
    } else
        bb_say(196, "HOLD OCT- AND OCT+ 3 S: PLAY AGAIN", 1, 0xff78u);
    if (ota_boot_cleanup_rc < 0)
        bb_say(214, ota_boot_cleanup_rc == OTA_ERR_CLEANUP ? "UPDATE CLEANUP FAILED" : "UPDATE CHECK FAILED", 1, 0xb189u);
    lcd_sync();
    lcd_blit(0, 0, 240, 240, framebuffer);
}

static void bb_safe_mode(void)            /* never returns */
{
    bb_hold h = {0, 0};
    uint32_t drawn = 0;
    fm1_audio_stop();
    for (;;) {
        fm1_wdt_feed();
        usb_retry(milliseconds);
        bb_boot_healthy(&bb_boot_rec, milliseconds);
        bb_service(0, 0, 0);
        bb_bank_console_service(0, 0, 0);
        {
            uint32_t moved = 0;
            for (uint32_t i = 0; i < BB_ENCODERS; i++)
                if (fm1_enc_take(i))
                    moved |= 1u << i;
            bb_selftest_inputs(&bb_test, fm1_in.notes, fm1_in.buttons, moved, fm1_adc_read(FM1_ADC_MASTER));
            for (uint32_t i = 0; i < 41u; i++)   /* LEDs 0-13 buttons, 14-40 keys: lit while pressed */
                fm1_led_key(i, i < 14u ? (int)((fm1_in.buttons >> i) & 1u) : (int)((fm1_in.notes >> (i - 14u)) & 1u));
        }
        uint32_t act = bb_hold_step(&h, (fm1_in.buttons & 3u) == 3u, milliseconds, BB_BOOT_SAFE);
        if (act == 1)
            bb_reset(0);                 /* back to normal play */
        if (act == 2)
            bb_uboot();
        if (milliseconds - drawn >= 200u) {
            drawn = milliseconds;
            bb_safe_draw(&h);
        }
    }
}
