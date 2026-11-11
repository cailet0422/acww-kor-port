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
> **Early days (0.11.0).** The game runs in the port, on phones too, and its frames are the
> decomp's own bit for bit on x86 and ARM, 32- and 64-bit ([docs/64bit.md](docs/64bit.md)).
> Sound: [docs/sound.md](docs/sound.md). See the [roadmap](docs/roadmap.md).

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
- the **tall view**: the 3D scene continued onto the upper screen, so the two screens show one
  picture ([docs/tall-view.md](docs/tall-view.md));
- a **Vulkan renderer** (on where the device can): the 3D scene drawn by the GPU at the
  screen's own resolution, antialiased, with the game's 2D layers mixed in; or, in software,
  the scene at 2 or 3 times the DS's size (heavy) ([docs/renderer.md](docs/renderer.md));
- **playing together** (experimental): the DS's local wireless between ports over a network,
  through a room on one of the devices or on a relay; and Nintendo Wi-Fi Connection through
  the device's network, to the server a DNS of the player's choosing names
  ([docs/multiplayer.md](docs/multiplayer.md));
- **sound**: the runtime's own mixer heard through an SDL audio device
  (`port/src/sound_sdl.c`; the decomp's device is Windows-only);
- the **ROM check and split** done on the device (`port/src/rom_prep.c`, the same split as the
  decomp's `tools/extract.py`, tested against it);
- small **stand-ins** for the decomp's x86-only host code, so its runtime builds for ARM and
  for 64-bit hosts (`port/src/native_os_portable.c`, `port/compat/`; [docs/64bit.md](docs/64bit.md));
- an **Android app** (`android/`): asks for the ROM once, then starts the game;
- **CI** that builds both and runs the tests on every push.

The port runs the Reference form: the game's code is interpreted, as on a DS.

## Builds

Every push builds an APK and a Linux program: open the latest run under
[Actions](https://github.com/cailet0422/acww-kor-port/actions/workflows/build.yml) and download
its artifacts. There are no releases yet.

| Platform | Needs |
|---|---|
| Android | Android 5.0 or later, ARM 32- or 64-bit, OpenGL ES 2.0 |
| Linux | x86-64 with SDL2 (`libsdl2-2.0-0`); a 32-bit x86 build is made too |

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

On Android the mark with three dots opens the settings (in Korean, or English where the
system is in English): sound, autosave, Mr. Resetti, the full-touch mode (no buttons, the stylus alone),
the tall view, the arranging of the buttons, the save exported and imported, another ROM.
Arranging, drag each button where you want it, the picture included, make the one you touched
last smaller or larger with − and +, put all back with RESET, and close with the tick; tall
and wide screens keep their own arrangements. Back asks whether to leave the game. (On a touch
screen elsewhere, a tap on the mark turns the buttons off and on and a hold opens the
arranging.) Tab held
runs as fast as the machine can; F12 saves the screens as a PNG next to the save.

The port saves the town by itself, every minute the game would let you save and when you
leave the app, to a file beside the save; the next start asks whether to go on from it
([docs/autosave.md](docs/autosave.md); `autosave=0` or `--no-autosave` turns it off). Mr.
Resetti does not come after a play that ended without a save (`resetti=1` in `settings.txt`,
or `--resetti`, brings him back as on a DS).

The save is a 256 KB flash image (the `.sav` format emulators use): `acww.sav` in the app's
files on Android, in `~/.local/share/acww-kor-port/acww-kor/` on Linux (`--data` and a save path
change both). It is written once the game stops writing to the chip, through a temporary file,
and the first save over an existing file keeps it as `.bak`.

## License

The port's own code (`port/`, `android/`, the CI) is under the [MIT License](LICENSE).
acww-decomp, in `decomp/`, is under [CC0 1.0](https://github.com/banatic/acww-decomp/blob/main/LICENSE);
the front end follows its `runtime/play.c`. SDL2 (fetched at build time) is under the zlib
license. *Animal Crossing* is Nintendo's; this project is not affiliated with Nintendo.
