/* SPDX-License-Identifier: GPL-3.0-only
 * Showbill screens: DORMANT (stopped), PLAY (the existing control pages) and LISTEN (scenes,
 * broadcast cuts, rising notes, cut-out players). Display-only: reads the engine, never writes it. */
#ifndef SW_SHOWBILL_H
#define SW_SHOWBILL_H
#include "display.h"
enum { SB_DORMANT, SB_PLAY, SB_LISTEN };
enum { SB_PREF_AUTO, SB_PREF_PLAY, SB_PREF_WATCH };
/* The nine reviewed players (#73). Steel, guitar and piano chairs rotate through the players who
 * hold them; Tommy sings and Tiny plays mandolin, so they enter on Bob's named calls. */
enum { SB_NONE, SB_LEON, SB_ELDON, SB_AL, SB_HERB, SB_TINY, SB_TOMMY, SB_JUNIOR, SB_NOEL, SB_JIMMY, SB_FIGURES };
#define SB_NOTES 16u
#define SB_IDLE_MS 8000u
typedef struct { uint8_t player, pitch, side, live; uint16_t age; } sb_note;
typedef struct {
 uint32_t ms, idle_ms, last_frames, song_revision, shot_ms;
 uint16_t until_bar, song_bar0, solo_bar0;
 uint8_t pref, shot, who, next, in_solo, calling, steel_turn, note_next, guitar_turn;
 uint8_t pose[SB_FIGURES];
 uint16_t steel_quiet_ms; uint8_t last_beat;
 sb_note notes[SB_NOTES];
} sw_showbill;
void sw_showbill_init(sw_showbill *s);
/* Any button, key or knob: show the controls again and restart the idle timer. */
void sw_showbill_touch(sw_showbill *s);
void sw_showbill_prefer(sw_showbill *s,uint32_t pref);
/* Call from the main loop with the milliseconds since the last call. Reads note-ons
 * from the engine's voices (consumed audio), the bar clock, solos and Bob's calls. */
void sw_showbill_tick(sw_showbill *s,const sw_engine *e,uint32_t elapsed_ms);
uint32_t sw_showbill_mode(const sw_showbill *s,const sw_engine *e);
/* Shared target/host arbitration: stopped touches still expose SONGS/KEEPS,
 * and a held note/button keeps its controls visible beyond the idle timeout. */
uint32_t sw_showbill_panel_visible(const sw_showbill *s,const sw_engine *e,const sw_panel *p);
/* Draws DORMANT or LISTEN. In PLAY the platform keeps drawing sw_panel_display(). */
void sw_showbill_draw(const sw_showbill *s,const sw_engine *e,sw_rectangle r,void *c);
/* Art comes through one table. On the device it is filled from the region-B art bank (M1a) once that bank
 * validates, and stays NULL otherwise: every screen then draws its no-bank form (linked Bob title and
 * portrait, plain sets, name cards instead of figures), so a missing or wrong bank never blanks the screen.
 * Host and emulator builds link the art and install it with sw_showbill_linked_art(). */
typedef struct sb_image_s {
 uint16_t width,height;
 const uint16_t *rows;const uint8_t *runs;const uint16_t *palette;
 /* Only a validated packed-bank view sets these; no wire-native pointers. */
 const uint8_t *bank_base,*bank_entry;
} sb_image_t;
typedef struct {
 const sb_image_t *bg[3];                 /* ballroom, radio, road */
 const sb_image_t *stage[SB_FIGURES][3], *corner[SB_FIGURES][3], *close[SB_FIGURES][3];
 const sb_image_t *bob_speak, *bob_idle;
} sb_art;
void sw_showbill_set_art(const sb_art *art);   /* NULL = no bank */
const sb_art *sw_showbill_art(void);
#ifdef SW_SHOWBILL_LINKED_ART
void sw_showbill_linked_art(void);
#endif
#endif
