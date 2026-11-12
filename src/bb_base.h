/* SPDX-License-Identifier: GPL-3.0-only
 * BB-BOB base machine: the parts every build must have before any instrument feature.
 *   1. USB MIDI always enumerated (vendor/fm1/src/usb.c), retried once a second without a cable.
 *   2. A way home that does not need the mask-ROM: the M-UPGRADE entry (vendor/fm1/src/ota.c), the
 *      same route BB1 / Felucca use, so the stock updater or our Mac tool can install BB1 1.5.
 *   3. Remote control and telemetry over SysEx: identity, STOP + master zero, counters.
 *   4. SAFE MODE instead of UBOOT: a crash, two failed boots or OCT- + OCT+ held 5 s restart into a
 *      screen with audio off and only USB + M-UPGRADE running. UBOOT stays only as the last resort
 *      (two failed SAFE MODE boots, OCT- + OCT+ held 15 s in SAFE MODE, or the host's stock soft key).
 * This header and bb_base.c are pure logic (no registers): the host tests run them unchanged.
 * SysEx (7D = non-commercial / development ID):  F0 7D 42 42 <cmd> [7-bit data] F7
 *   01 IDENTITY   -> F0 7D 42 42 01 <ASCII identity> F7
 *   02 STOP       stop the band, master to 0 and held there (the pot is ignored)  -> ack
 *   03 TELEMETRY  -> F0 7D 42 42 03 <pack7 of BB_TELEMETRY_WORDS little-endian words> F7
 *   04 RELEASE    give the master back to the pot                                 -> ack
 *   05 SAFE       restart into SAFE MODE (from SAFE MODE: restart normally)       -> ack
 *   06 PING       -> ack
 *   07 SELFTEST   -> F0 7D 42 42 07 <pack7 of the I/O self-test record> F7 (see bb_selftest)
 *   08 RESET      start a new measurement window: zero the timing/miss counters (stack high-water kept),
 *                 bump the epoch so a host can tell windows apart                 -> ack
 *   09 FAULT      -> F0 7D 42 42 09 <pack7 of BB_FAULT_WORDS words> F7: the latched crash record (survives
 *                 the reset into SAFE MODE): valid, count, vector, PC, RETS, EMU, DBG, SP, PSR, uptime, early
 *   0A PROFILE    diagnostic builds only: paged 64-bit stage totals and per-half peaks
 *   0B PROFILE SET diagnostic builds only: bit0 profile timers, bit1 force sampled voices to synth fallback,
 *                  bits2-3 display state (0 normal art, 1 no art, 2 LCD frozen; 3 refused)
 *   0C CONTEXT    peak-context builds only: page of peak timing and workload state
 *   0D CONTEXT SET stopped/master-zero only: 0 normal display, 1 frozen, 2 start, 3 reboot;
 *                  16 load Spanish Two Step at Then, 17 Tulsa at Now, 18 Tulsa at Now/Funk
 * IDENTITY carries the full 64-hex-digit application SHA-256 when built with SW_BOB_FULL_SHA.
 *   ack: F0 7D 42 42 7F <cmd> <0 ok | 1 refused> F7 */
#ifndef BB_BASE_H
#define BB_BASE_H
#include <stdint.h>

#define BB_SX_ID0 0x7Du
#define BB_SX_ID1 0x42u
#define BB_SX_ID2 0x42u
enum { BB_CMD_IDENTITY = 1, BB_CMD_STOP = 2, BB_CMD_TELEMETRY = 3, BB_CMD_RELEASE = 4, BB_CMD_SAFE = 5,
       BB_CMD_PING = 6, BB_CMD_SELFTEST = 7, BB_CMD_RESET = 8, BB_CMD_FAULT = 9, BB_CMD_PROFILE = 10,
       BB_CMD_PROFILE_SET = 11, BB_CMD_CONTEXT = 12, BB_CMD_CONTEXT_SET = 13, BB_CMD_ACK = 0x7F };
enum { BB_PROFILE_TIMERS = 1u, BB_PROFILE_FORCE_SYNTH = 2u, BB_PROFILE_DISPLAY_SHIFT = 2u,
       BB_PROFILE_DISPLAY_NORMAL = 0u, BB_PROFILE_DISPLAY_NO_ART = 1u, BB_PROFILE_DISPLAY_FROZEN = 2u,
       BB_PROFILE_DISPLAY_MASK = 3u << BB_PROFILE_DISPLAY_SHIFT, BB_PROFILE_DISPLAY_SETTLED = 16u };
enum { BB_BOOT_NORMAL = 0, BB_BOOT_SAFE = 1, BB_BOOT_UBOOT = 2 };
#define BB_BOOT_MAGIC 0x42425342u          /* "BBSB" */
#define BB_HEALTHY_MS 30000u               /* a boot that lasts this long is good */
#define BB_HOLD_SAFE_MS 5000u              /* normal: OCT- + OCT+ held -> SAFE MODE */
#define BB_HOLD_LEAVE_MS 3000u             /* SAFE MODE: held -> restart normally */
#define BB_HOLD_UBOOT_MS 15000u            /* SAFE MODE: held this long -> mask-ROM UBOOT (last resort) */

/* lives in .noinit: survives a core reset, not a power cycle */
typedef struct { uint32_t magic, failures, pending, mode, safe_req, safe_fail; } bb_boot;
/* Boot decision. Counts the previous boot as failed if it never reached BB_HEALTHY_MS. */
uint32_t bb_boot_decide(volatile bb_boot *b);
/* Main loop, every pass: marks the boot healthy once it has lasted BB_HEALTHY_MS. */
void bb_boot_healthy(volatile bb_boot *b, uint32_t uptime_ms);
/* Ask for a restart into SAFE MODE (1) or out of it (0); an intentional reset is never a failure. */
void bb_boot_request_safe(volatile bb_boot *b, uint32_t on);

/* OCT- + OCT+ hold timer. Returns the action now due: 0 none, 1 toggle SAFE MODE, 2 UBOOT. */
typedef struct { uint32_t since, held; } bb_hold;
uint32_t bb_hold_step(bb_hold *h, uint32_t both_down, uint32_t now_ms, uint32_t mode);
/* Seconds left before the hold acts (for the countdown), 0 when none is showing. */
uint32_t bb_hold_countdown(const bb_hold *h, uint32_t now_ms, uint32_t mode);

/* Remote master hold: after STOP the pot is ignored until RELEASE, or until the pot is turned more
 * than BB_POT_WAKE (of 1023) from where it was, so a player at the unit can always take it back. */
#define BB_POT_WAKE 80
typedef struct { int32_t held, pot_at_hold; } bb_master;
void bb_master_hold(bb_master *m, int32_t pot);
void bb_master_release(bb_master *m);
/* Master percentage to apply for this pot reading, or -1 to leave the master alone. */
int32_t bb_master_from_pot(bb_master *m, int32_t pot);

/* Telemetry words, in reply order. */
enum { BB_T_UPTIME_MS, BB_T_HALVES, BB_T_PEAK_TICKS, BB_T_OVER_BUDGET, BB_T_DMA_MISSES, BB_T_ENTRY_PEAK,
       BB_T_MASK_PEAK, BB_T_USER_STACK, BB_T_IRQ_STACK, BB_T_USB_RESETS, BB_T_USB_RX, BB_T_USB_TX,
       BB_T_USB_STATE, BB_T_BOOT, BB_T_PLAYING, BB_T_MASTER, BB_T_MIDI_USB_IN, BB_T_MIDI_TRS_IN, BB_T_MIDI_DROPS,
       BB_T_EPOCH, BB_T_EPOCH_MS, BB_TELEMETRY_WORDS };
#define BB_FAULT_WORDS 11u

/* Parsed request: frame = the 7-bit bytes between F0 and F7. Returns the command, or 0 if the frame
 * is not ours (so ota.c may take it). */
uint32_t bb_sx_command(const uint8_t *frame, uint32_t n);
/* Replies, written as complete F0..F7 messages. Each returns its length (out must hold 160 bytes). */
uint32_t bb_sx_ack(uint8_t *out, uint32_t cmd, uint32_t refused);
uint32_t bb_sx_identity(uint8_t *out, const char *identity);
uint32_t bb_sx_telemetry(uint8_t *out, const uint32_t *words);
uint32_t bb_pack7(const uint8_t *in, uint32_t n, uint8_t *out);
/* Any word record: F0 7D 42 42 <cmd> pack7(little-endian words) F7. n <= 26. */
uint32_t bb_sx_words(uint8_t *out, uint32_t cmd, const uint32_t *words, uint32_t n);

/* I/O self-test: which of the 27 keys, 14 buttons and 7 encoders have been seen working since boot, the
 * master pot's range, and MIDI arriving on USB and TRS. RETURN MODE shows it live (each pressed key lights
 * its own LED), so a unit can be checked by hand with no computer; SysEx SELFTEST reports it. */
#define BB_KEYS 27u
#define BB_BUTTONS 14u
#define BB_ENCODERS 7u
typedef struct {
    uint32_t keys_seen, buttons_seen, enc_seen;   /* bit per control, set once it changed */
    uint32_t keys_now, buttons_now;               /* current levels */
    int32_t pot_min, pot_max, pot_now;            /* -1 until the first reading */
    uint32_t midi_usb, midi_trs, midi_drops;      /* messages in, by source; lost to overflow */
} bb_selftest;
void bb_selftest_init(bb_selftest *t);
void bb_selftest_inputs(bb_selftest *t, uint32_t keys, uint32_t buttons, uint32_t enc_moved, int32_t pot);
void bb_selftest_midi(bb_selftest *t, uint32_t source);   /* 1 USB, 2 TRS (usb.c / midi_uart.c) */
/* Counts seen so far, for the screen: keys, buttons, encoders; pot travel in percent of full scale. */
uint32_t bb_bits(uint32_t v);
uint32_t bb_selftest_pot_travel(const bb_selftest *t);
uint32_t bb_sx_selftest(uint8_t *out, const bb_selftest *t);
#endif
