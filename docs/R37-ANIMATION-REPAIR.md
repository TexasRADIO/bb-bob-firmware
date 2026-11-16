# R37 animation repair

This release fixes a native input-loop defect in R36: the 25 ms MASTER read reset the show and panel idle clocks even at an unchanged knob position. The hardware and browser now share sw_panel_master_poll. Only meaningful control movement restarts the panel display timer; one-percent ADC chatter does not keep the show hidden.

During playback, controls yield to the stage show after eight idle seconds and after 2.5 seconds following a band cue. The original 50 songs, embedded calls, voice engine, display cadence, loader, timbre bank and artwork bank are retained.

The exact known-chip predicate from the released BB1 code admits the original Puya and later UC25HQ80 parts; all other JEDEC IDs remain refused. This repairs the application updater/bank-write admission gap. It does not establish the chip fitted to any particular user's FM-1 or diagnose audio distortion.

Regression tests exercise the shared input code with continuous 40 Hz volume readings, stationary button/key polling, one-percent ADC jitter, the released CRC-valid artwork, changing rendered frames across all 50 Classics and original Then/Now/Funk arrangements, cue return and damaged-art rejection. Native builds and complete-package verification check the firmware identity, loader and bank ranges. No physical FM-1 was contacted in this repair build; host timing is not device audio qualification.

Reproduce the target with Emscripten-independent PI32 toolchain20260730.1 and the existing build container:

```sh
python3 tools/build_target.py --base-machine --output-dir build/r37-complete --frozen-cue-sha256 7d436dfdbaa7abf6d6c5028a011e2a41002360dbecd951de1ec20b1496215da6
make test
```

Reproduce the browser with Emscripten6.0.13 using tools/build_browser.py. The generated player and native package use the same R37 identity and source tree.
