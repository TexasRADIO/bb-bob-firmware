# Big Beat Studio BB-BOB R37

BB-BOB is a Western swing bandstand for the M-VAVE FM-1. Choose one of50 tunes, generate an original, cue the band and bring in acid bass and drum-machine beats with Then / Now.

## Install and play

- [Install BB-BOB](https://thebigbeat.app/bb-bob/install/)
- [Play in a browser](https://thebigbeat.app/bb-bob/)
- [Playing guide](https://thebigbeat.app/bb-bob/manual.html)
- [USB editor](https://thebigbeat.app/editor/?product=5&mode=midi)

The complete FWSC contains the application, timbre bank and art bank. It does not require BB1. Its product identity is FM-1_92361037.

## R37 repair

The R36 volume-polling bug kept the animated show hidden. Hardware and browser now share a polling handler that preserves the idle timer. During playback, the show appears after eight idle seconds and after2.5seconds following a band cue. The original50songs, embedded calls, sound engine, update loader and both banks are retained. Exact known FM-1 flash IDs from BB1's published support are recognized; unknown chips remain refused.

HOME opens Dance Hall; SEQ opens Play Keys. ALGORITHM moves Then/Now and LFO jumps between the ends. SEL opens BAND, where KNOB3 controls FUNK. The shared USB editor supports the current session and transport. Keeps remain temporary.

## Build and checks

Use PI32 toolchain20260730.1 and the pinned build container; see docs/R37-ANIMATION-REPAIR.md. The frozen cue header is retained at build/r37-complete/leon_pcm.h.

```sh
python3 tools/build_target.py --base-machine --output-dir build/r37-complete --frozen-cue-sha256 7d436dfdbaa7abf6d6c5028a011e2a41002360dbecd951de1ec20b1496215da6
make test
```

The target output must match release/RELEASE.json. Build the browser with Emscripten6.0.13 and tools/build_browser.py. Host checks cover the actual artwork bank, changing frames under continuous volume/input polling, cue handback, damaged-art refusal, editor and protected loader write ranges. These are not a physical-unit audio qualification.

## Licensing

Original firmware code is GPL-3.0-only. See LICENSE and LICENSING.md. Imported board code retains its upstream notices. Artwork, historical recordings, fonts and musical editions retain their separate rights.
