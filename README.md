# BB-BOB R36 browser maintenance source

Corresponding source for the R36 browser-player maintenance update. The browser now uses the Dance Hall controls and show, with a corrected idle timer for MASTER polling. This is not a new FM-1 firmware release. The native R36 package and its sound and art banks remain unchanged; their frozen source is at https://github.com/TexasRADIO/bb-bob-firmware/tree/r36 .

## Build the browser

Install Emscripten 6.0.13 and its JavaScript dependencies, then run:

```sh
python3 tools/build_browser.py --emcc /path/to/emcc
```

The player is written to build/browser-player. The committed web assets include the same public call, timbre and art banks. Their separate rights and original attribution are retained.

The source includes the proposed matching native MASTER-polling correction. It has host regression coverage; it has not been built or qualified as a replacement native firmware in this update. Do not treat the browser result as a hardware audio result.

## Checks

The MASTER-polling regression covers a steady level, one-percent ADC jitter, retained volume and the show returning after a cue. The existing Dance Hall and editor tests also run. The website has release-linked tests that reject an older browser identity even when its own asset hashes agree.

## Licensing

Original firmware code is GPL-3.0-only. See LICENSE and LICENSING.md. Imported board code retains its upstream notices. Artwork, historical recordings, fonts and musical editions retain their separate rights.
