/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_SWING_H
#define BB_SWING_H
#include <stdint.h>
#ifdef SW_RENDER_PROFILE
#include "render_profile.h"
#endif
#ifdef SW_WITH_BANK
#include "sample_player.h"
#define SW_CALL_CAPACITY 384u
#define SW_CALL_POOLS 8u
typedef uint16_t sw_call_index;
int sw_calls_bank(const uint8_t *bytes,uint32_t length,uint32_t limit);
#else
#define SW_CALL_CAPACITY 128u
#define SW_CALL_POOLS 3u
typedef uint8_t sw_call_index;
#endif
#define SW_RATE 44100u
#define SW_VOICES 24u
#define SW_BARS 128u
#define SW_PLAYERS 8u
#define SW_STRING_SAMPLES 512u
enum { SW_STEEL, SW_FIDDLE, SW_GUITAR, SW_PIANO, SW_BASS, SW_DRUMS, SW_HORNS, SW_SYNTH };
enum { SW_KICK=8, SW_SNARE, SW_HAT, SW_CLAP, SW_OPEN_HAT, SW_PAD, SW_ARP, SW_BELL };
enum { SW_INTRO, SW_THEME, SW_BRIDGE, SW_SOLO, SW_RETURN, SW_ENDING };
enum { SW_PLAY, SW_NEW, SW_LEON, SW_FILL, SW_STOPTIME, SW_HOME, SW_END, SW_PLAYER,
       SW_TEMPO, SW_SWING, SW_MODERN, SW_AIR, SW_GLOW, SW_SWELL, SW_KEY, SW_FORM, SW_MASTER, SW_NOTE, SW_BAND, SW_CUE, SW_HOLLER, SW_RANDOM_CALL, SW_TITLE, SW_AUTO_CALL, SW_CALL_PAD, SW_PAD_MODE, SW_LEON_RECORD, SW_YEAH, SW_CALL_PAGE, SW_SCORE_SELECT, SW_SCORE_PREVIEW, SW_SOURCE_SOLOS, SW_TEXTURE, SW_SCORE_FEATURE, SW_CLASSIC_SELECT, SW_CLASSIC_MODE, SW_AUDITION, SW_FIDDLE_COLOUR, SW_CALL_FAMILY, SW_SAMPLE_SELECT, SW_SAMPLE_PLAY, SW_SAMPLE_STOP, SW_HIPHOP };
typedef struct { uint8_t root, quality; } sw_chord;
typedef struct { sw_chord chord[2]; uint8_t section, melody[8], mask, phrase_id, lead[16], gate[16]; uint16_t lead_mask; } sw_bar;
typedef struct { uint32_t seed; uint8_t key, form, count, beats, classic_id; sw_bar bars[SW_BARS]; } sw_song;
typedef struct {
    uint32_t phase, phase2, increment, target, age, gate, life;
    float amplitude, envelope, decay, filter, pan, bend_step;
    float pan_left, pan_right;
    float string_period, string_target, string_slide, string_previous, string_output, body1, body2;
    float sample_position, sample_scale;
    uint16_t string_position, onset_delay;
    uint8_t type, player, active, bending, string_half, sample_bank, manual_key, manual_voice, foreground;
} sw_voice;
typedef struct {
    uint32_t phase, gate;
    float pitch, target, envelope, filter_envelope, pole[4], accent;
    float sweep;           /* accent sweep: builds on close accents, the 303's scream */
    uint8_t active, sliding, prev_slide;
} sw_acid;
typedef struct {
    sw_song song, pending;
    #ifdef SW_RENDER_PROFILE
    sw_render_profile profile;
    #endif
    sw_voice voice[SW_VOICES];
    int16_t string_delay[SW_VOICES][SW_STRING_SAMPLES];
    sw_acid acid;
    float modern_target, pump;
    float hiphop, hiphop_target;   /* HIP-HOP style weight 0..1: how the band plays, not what it plays on */
    uint32_t dance_clock, dance_length, dance_split, acid_notes, acid_slides, dance_steps, pad_notes, arp_notes;
    sw_chord dance_chord;
    uint8_t dance_quarter, dance_sub, dance_allowed, dance_final, synth_manual;
    float record_position, record_grain[2], record_pitch, record_time_step;
    uint32_t record_clock;
    uint16_t record_until, record_release;
    uint8_t record_active, record_pending;
    float delay[4096], master, master_target, air, modern, glow, swell, call_envelope;
    uint32_t noise, sample, tick_remaining, delay_pos, clock_remainder;
    uint32_t notes_started[SW_PLAYERS], voice_steals, rendered_frames, song_revision, cue_position, call_random;
    uint16_t bpm, swing, pending_bpm, pending_swing, bar, tick, solo_from, solo_until;
    uint8_t guitar_voicing[3], guitar_voiced;
    sw_chord guitar_chord;
    uint32_t lead_wait, score_tick, score_fraction, score_notes;
    uint16_t score_selected, score_choice, score_event, score_scheduled;
    uint32_t classic_offset, classic_at, classic_notes, tick_length;
    uint8_t classic_choice, classic_mode, classic_request, classic_cycle;
    uint8_t lead_second, score_playing, score_arrangement, source_solos, texture, call_page;
    sw_call_index call_bag[SW_CALL_POOLS][SW_CALL_CAPACITY], call_remaining[SW_CALL_POOLS], call_last[SW_CALL_POOLS];
    uint32_t call_decoded, call_bit_position;
    int32_t call_predictor, call_step_index, call_previous;
    #ifdef SW_WITH_BANK
    sw_sample_player sample_player;
    uint16_t sample_selected;
    #endif
    uint8_t playing;
    volatile uint8_t pending_ready;
    uint8_t players, fill, stop_bars, ending, leon_pending, callout;
    uint8_t key_next, form_next, spoken, home_pending;
    sw_call_index cue_kind;
    uint8_t title, auto_calls, pad_mode, fiddle_colour, snare_take;
    uint32_t visual_ms, visual_random, gesture_wait, gesture_time, walk_clock;
    uint8_t gesture, gesture_active, walking;
} sw_engine;
void sw_generate(sw_song *song, uint32_t seed, uint8_t key, uint8_t form);
void sw_init(sw_engine *e, uint32_t seed);
/* Producer prepares a song outside the audio consumer; publish pending_ready last.
 * The platform serializes publication, commands and snapshots with its audio ISR. */
int sw_prepare(sw_engine *e, uint32_t seed);
int sw_prepare_classic(sw_engine *e,uint32_t index);
int sw_prepare_classic_seed(sw_engine *e,uint32_t index,uint32_t seed);
int sw_prepare_recall(sw_engine *e,uint32_t seed,uint32_t classic_id,uint16_t bpm,uint16_t swing);
uint32_t sw_classic_slots(void);
uint32_t sw_classic_available(uint32_t index);
uint32_t sw_classic_ready_count(void);
const char *sw_classic_title(uint32_t index);
/* Read-only catalogue metadata for the foreground song list. */
uint32_t sw_classic_preview(uint32_t index,uint8_t *key,uint16_t *tempo);
uint32_t sw_call_metadata_flags(uint32_t index);
void sw_command(sw_engine *e, uint32_t command, int32_t value);
/* Live keyboard tags are separate from arrangement voices. Release by physical
 * key, so octave/voice changes cannot strand a sounding note. */
void sw_key_on(sw_engine *e,uint32_t key,uint32_t pitch,uint32_t player);
void sw_key_off(sw_engine *e,uint32_t key);
void sw_keys_release(sw_engine *e);
/* Target-only boot cache for the small period/rhythm samples that otherwise
 * share the XIP path with code. Host builds leave the original arrays in use.
 * Call before audio/scan starts; the watchdog hook may be NULL. */
void sw_period_timbres_boot(void (*watchdog)(void));
/* Optional target embedded-call metadata/decoder table copy, before audio IRQs. */
void sw_call_tables_boot(void (*watchdog)(void));
void sw_render(sw_engine *e, float *stereo, uint32_t frames);
const char *sw_section_name(uint32_t section);
const char *sw_section_label(const sw_engine *e);
const char *sw_player_name(uint32_t player);
const char *sw_player_label(const sw_engine *e,uint32_t player);
void sw_status_json(const sw_engine *e, char *out, uint32_t capacity);
const char *sw_call_label(const sw_engine *e);
uint32_t sw_call_count(void);
uint32_t sw_calls_historic(void);
uint32_t sw_call_active(const sw_engine *e);
uint32_t sw_call_targets(const sw_engine *e);
uint32_t sw_sample_count(void);
uint32_t sw_sample_active(const sw_engine *e);
uint32_t sw_sample_selected(const sw_engine *e);
uint32_t sw_sample_position(const sw_engine *e);
uint32_t sw_sample_playing(const sw_engine *e);
uint32_t sw_sample_targets(const sw_engine *e);
uint32_t sw_bob_frame(const sw_engine *e);
const char *sw_call_short_label(uint32_t index);
uint32_t sw_record_available(void);
uint32_t sw_score_count(void);
const char *sw_score_short_label(uint32_t index);
/* Independent cosmetic clock; never consumes planning, voice or call randomness. */
void sw_visual_advance(sw_engine *e,uint32_t milliseconds);
#endif
