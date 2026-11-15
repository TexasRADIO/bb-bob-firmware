![BB-BOB](media/bb-bob-card.png)

# Big Beat Studio BB-BOB

BB-BOB is a Western swing groovebox for the M-VAVE FM-1. Pick one of 50 tunes or generate an original, cue the steel solo, call the bridge and turn **Then / Now** to bring in acid bass and drum-machine beats.

## Install and play

- [Install BB-BOB](https://thebigbeat.app/bb-bob/install/)
- [Try BB-BOB in a browser](https://thebigbeat.app/bb-bob/)
- [Product page and guide](https://thebigbeat.app/bb-bob/about/)

The release contains one Hub-compatible `.fwsc`. It writes the BB-BOB application and its matching timbre and art banks in one package. It does not require BB1.

## Hardware verification

The R34 single-package Hub release was installed on a physical FM-1. It booted
as `FM-1_92201029`, returned successfully to official V15, then installed and
booted again from V15. R35 reserves the independent BB-BOB identity
`FM-1_92361035`; its application, loader, banks and protected write ranges are
rebuilt and checked from the tagged source before release.

## Build

Use the PI32r3 toolchain and AC79 SDK with `JIELI_TOOLCHAIN` and `AC79_SDK` set. The source and generated build inputs for the released application are included here. A clean source build reproduces the released application and RAM loader byte for byte. `tools/package_full_install_r117.py` makes the app package; `tools/embed_banks.py` turns it into the single-package Hub release by embedding the pinned timbre and art banks at their fixed flash offsets.

## Licensing

The original firmware code is GPL-3.0-only. See [LICENSE](LICENSE) and [LICENSING.md](LICENSING.md). Historical recordings, character art, fonts and music editions have separate rights and are not relicensed by the GPL.

BB-BOB is an independent project. M-VAVE and FM-1 are trademarks of their respective owners.
