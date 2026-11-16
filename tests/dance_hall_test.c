/* SPDX-License-Identifier: GPL-3.0-only */
#include "swing.h"
#include "panel.h"
#include "showbill.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static sw_engine engine;
static sw_panel panel;
static sw_showbill show;
static float audio[1024];
static void reset(void) {
    sw_init(&engine, 301026);
    sw_panel_init(&panel);
    sw_showbill_init(&show);
    engine.title = 0;
}
static void advance(unsigned milliseconds) {
    sw_panel_tick(&panel, milliseconds);
    sw_panel_observe(&engine, &panel);
    sw_showbill_tick(&show, &engine, milliseconds);
}
int main(void) {
    reset();
    assert(sw_classic_ready_count() == 50);
    assert(strcmp(sw_panel_page_name(SWP_DIRECT), "DANCE HALL") == 0);
    sw_panel_button(&engine, &panel, SWP_HOME_BUTTON);
    assert(panel.page == SWP_DIRECT);
    sw_panel_button(&engine, &panel, SWP_SEQ);
    assert(panel.page == SWP_HOME);
    sw_panel_button(&engine, &panel, SWP_HOME_BUTTON);
    sw_panel_button(&engine, &panel, SWP_PLAY);
    assert(engine.playing);
    for (unsigned i = 0; i < 9; ++i) advance(1000);
    assert(!sw_showbill_panel_visible(&show, &engine, &panel));
    sw_panel_button(&engine, &panel, SWP_FX);
    sw_showbill_touch(&show);
    assert(sw_showbill_panel_visible(&show, &engine, &panel));
    advance(1000); advance(1000); advance(500);
    assert(!sw_showbill_panel_visible(&show, &engine, &panel));
    sw_panel_button(&engine, &panel, SWP_FX);
    advance(200);
    sw_panel_button(&engine, &panel, SWP_FX);
    assert(engine.stop_bars == 2);
    sw_panel_button(&engine, &panel, SWP_LFO);
    assert(engine.modern_target == 1.f && sw_panel_then_now_notice(&panel));
    sw_panel_button(&engine, &panel, SWP_LFO);
    assert(engine.modern_target == 0.f);
    sw_panel_button(&engine, &panel, SWP_SEL);
    sw_panel_encoder(&engine, &panel, 4, 25);
    assert(engine.hiphop_target == .5f);
    assert(strcmp(sw_panel_knob_label(SWP_BAND, 2), "FUNK") == 0);
    sw_panel_button(&engine, &panel, SWP_HOME_BUTTON);
    sw_panel_key(&engine, &panel, 1, 1);
    unsigned notes = 0;
    for (unsigned i = 0; i < SW_VOICES; ++i) notes += engine.voice[i].manual_voice;
    assert(notes > 0 && panel.page == SWP_DIRECT);
    sw_panel_key(&engine, &panel, 1, 0);
    unsigned revision = engine.song_revision;
    sw_panel_encoder(&engine, &panel, 6, 1);
    sw_panel_service(&engine, &panel);
    assert(panel.page == SWP_DIRECT && panel.queued_kind == SWP_Q_SONG);
    assert(engine.song_revision == revision);
    if (engine.classic_request && !engine.pending_ready) {
        engine.classic_request = 0;
        sw_prepare_classic(&engine, engine.classic_choice);
    }
    for (unsigned i = 0; i < 1000 && engine.song_revision == revision; ++i) sw_render(&engine, audio, 512);
    assert(engine.song_revision != revision);
    advance(100);
    sw_showbill_on_screen(&show, 0);
    show.shot = 1; show.who = SB_LEON; show.until_bar = engine.bar;
    advance(100);
    assert(show.shot && show.until_bar > engine.bar);
    sw_showbill_on_screen(&show, 1);
    engine.playing = 0;
    advance(100);
    assert(show.curtain_ms || show.curtain_pending);
    reset();
    sw_panel_button(&engine, &panel, SWP_PLAY);
    sw_panel_observe(&engine, &panel);
    engine.bar = engine.song.count;
    engine.playing = 0;
    sw_panel_observe(&engine, &panel);
    assert(panel.encore_ms == 8000);
    sw_panel_button(&engine, &panel, SWP_PLAY);
    assert(engine.playing && panel.encore_ms == 0);
    puts("Dance Hall regression checks passed: 50 songs, HOME/SEQ, idle show, cue return, break, Then/Now, FUNK, live keys, queued tune, held shot, curtain");
    return 0;
}
