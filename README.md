# Big Beat Studio BB-BOB R36

BB-BOB is a Western swing bandstand for the M-VAVE FM-1. Choose one of 50 tunes, generate an original, cue the band and bring in acid bass and drum-machine beats with Then / Now.

## Install and play

- [Install BB-BOB](https://thebigbeat.app/bb-bob/install/)
- [Play in a browser](https://thebigbeat.app/bb-bob/)
- [Playing guide](https://thebigbeat.app/bb-bob/about/guide.html)

The release includes one complete FWSC containing the application, timbre bank and art bank. It does not require BB1. The package identity is FM-1_92361036. Install only on the FM-1.

## Dance Hall and editor

HOME opens Dance Hall. LFO switches Then/Now; BAND knob 3 controls FUNK. The show follows the tune, solos and band cues. This edition includes the shared editor protocol for reading the current session, reversible parameter edits, transport and connection checks.

Editable Keeps and session state remain in memory. This firmware does not read other firmwares' projects or sound libraries. Keep exported files and backups before changing firmware.

## Build

The firmware source, generated musical inputs, board source, package tools and licence notices are included. Use the PI32r3/FPU toolchain and the SDK described by the retained source manifests. The build uses a 128-frame half, the base-machine profile, XIP audio rendering and the frozen cue header recorded in build/RELEASE-INPUTS.json.

Set JIELI_TOOLCHAIN to the unpacked JieLi toolchain and use the build tool's Linux container environment. Run:

```sh
python3 tools/build_target.py --base-machine --output-dir build/r36-editor2 --frozen-cue-sha256 7d436dfdbaa7abf6d6c5028a011e2a41002360dbecd951de1ec20b1496215da6
```

The generated application must match build/RELEASE-INPUTS.json. The package loader and banks are pinned in release/RELEASE.json; tools/verify_release.py independently checks their identities and contents.

## Licensing

Original firmware code is GPL-3.0-only. See LICENSE and LICENSING.md. Imported board code retains its upstream notices. Artwork, historical recordings, fonts and musical editions retain their separate rights; inclusion does not relicense them under the GPL.

BB-BOB is independent of M-VAVE. FM-1 and M-VAVE are their owners' trademarks.
