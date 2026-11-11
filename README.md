<p align="center"><b>English</b> · <a href="README.ko.md">한국어</a></p>

<h1 align="center">acww-kor-port</h1>

<p align="center">
  An Android (and Linux) port of the Korean release of <i>Animal Crossing: Wild World</i> (Nintendo DS, <code>ADMK</code>),<br>
  built on the <a href="https://github.com/banatic/acww-decomp">acww-decomp</a> decompilation.
</p>

<p align="center">
  <a href="https://github.com/cailet0422/acww-kor-port/actions/workflows/build.yml"><img alt="build" src="https://github.com/cailet0422/acww-kor-port/actions/workflows/build.yml/badge.svg"></a>
</p>

> [!IMPORTANT]
> **No game data is included.** You need your own dump of the Korean cartridge (`ADMK`, SHA-1
> `1fffd601d3439e37cd934e72d08e487a93fc449b`; a dump trimmed to the used size is accepted too).
> The port reads the game's code from it on the device; nothing from the ROM is in this
> repository or in the builds.

> [!WARNING]
> **Early days (0.1.0).** The program builds for Android and Linux, and the ROM handling is
> tested, but the game has **not yet been run on a phone**. Sound is not there yet, and the
> APK installs only on phones that can run 32-bit apps. See the [roadmap](docs/roadmap.md).

## What it is

[acww-decomp](https://github.com/banatic/acww-decomp) decompiles the game's ARM9 code and has a
runtime that plays it on a PC: every function can run as the ROM's code in an interpreter
(the *Reference* form), as its matching source compiled for the PC, or as a readable rewrite.
Its window is Windows-only and its compiled forms need a 32-bit x86 PC.

This repository adds what the other platforms need, without changing the decomp (it is the
`decomp/` submodule):

- an **SDL2 front end** (`port/src/front_sdl.c`) in place of the decomp's Win32 window: both
  screens, keyboard, mouse, game controllers, touch buttons drawn around the screens, frame
  pacing and the save file;
- the **ROM check and split** done on the device (`port/src/rom_prep.c`, the same split as the
  decomp's `tools/extract.py`, tested against it);
- small **stand-ins** for the decomp's x86-only host code, so its runtime builds for ARM
  (`port/src/native_os_portable.c`, `port/compat/port_compat.h`);
- an **Android app** (`android/`): asks for the ROM once, then starts the game;
- **CI** that builds both and runs the tests on every push.

The port runs the Reference form: the game's code is interpreted, as on a DS.

## Builds

Every push builds an APK and a Linux program: open the latest run under
[Actions](https://github.com/cailet0422/acww-kor-port/actions/workflows/build.yml) and download
its artifacts. There are no releases yet.

| Platform | Needs |
|---|---|
| Android | Android 5.0 or later, a phone that runs 32-bit (`armeabi-v7a`) apps, OpenGL ES 2.0 |
| Linux | 32-bit x86 SDL2 (`libsdl2-2.0-0:i386`) on x86 Linux |

On Android, the first start asks for the ROM file and copies it into the app's storage. On
Linux, `acww-kor --rom path/to/your.nds`. Building yourself: [docs/building.md](docs/building.md).

## Controls

| DS | Keyboard | Game controller | Touch screen |
|---|---|---|---|
| +Control Pad | arrow keys | d-pad, left stick | pad on the left (below in portrait) |
| A, B, X, Y | X, Z, S, A | the face buttons by position | buttons on the right |
| L, R | Q, W | shoulders, triggers | top corners |
| START, SELECT | Enter, Shift or Backspace | Start, Back | bottom |
| touch screen | mouse on the lower screen | | a finger on the lower screen |

Tab held runs as fast as the machine can; F12 saves the screens as a PNG next to the save.

The save is a 256 KB flash image (the `.sav` format emulators use): `acww.sav` in the app's
files on Android, in `~/.local/share/acww-kor-port/acww-kor/` on Linux (`--data` and a save path
change both). It is written once the game stops writing to the chip, through a temporary file,
and the first save over an existing file keeps it as `.bak`.

## License

The port's own code (`port/`, `android/`, the CI) is under the [MIT License](LICENSE).
acww-decomp, in `decomp/`, is under [CC0 1.0](https://github.com/banatic/acww-decomp/blob/main/LICENSE);
the front end follows its `runtime/play.c`. SDL2 (fetched at build time) is under the zlib
license. *Animal Crossing* is Nintendo's; this project is not affiliated with Nintendo.
