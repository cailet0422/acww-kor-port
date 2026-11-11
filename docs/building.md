<p align="center"><b>English</b> · <a href="building.ko.md">한국어</a></p>

# Building

Clone with the decomp:

```sh
git clone --recursive https://github.com/cailet0422/acww-kor-port
# or, in a clone made without it:
git submodule update --init
```

No ROM is needed to build; the program asks for it when it starts.

## Linux

On Ubuntu 24.04:

```sh
sudo apt-get install build-essential cmake ninja-build libsdl2-dev
cmake -S port -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
build/acww-kor --rom path/to/your.nds
```

`build/acww-kor --help` lists the options. `-DSDL_SOURCE_DIR=/path/to/SDL` builds SDL2 from a
source tree instead of using the system's; `-DPORT_FRONT_END=OFF` builds only the headless
test programs, which need no SDL2.

The 32-bit x86 build is the one host with the decomp's own host code and its x86 block
translator (docs/64bit.md):

```sh
sudo dpkg --add-architecture i386 && sudo apt-get update
sudo apt-get install gcc-multilib g++-multilib libsdl2-dev:i386
cmake -S port -B build-i686 -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=$PWD/port/cmake/linux-i686.cmake
```

## Android

Needs JDK 17, the Android SDK with platform 34, NDK `27.2.12479018` and CMake `3.22.1`
(`sdkmanager "platforms;android-34" "ndk;27.2.12479018" "cmake;3.22.1"`), and the SDL2 source:

```sh
git clone --depth 1 --branch release-2.30.9 https://github.com/libsdl-org/SDL.git android/SDL
cd android
./gradlew assembleRelease      # app/build/outputs/apk/release/app-release.apk
```

The APK holds `armeabi-v7a` and `arm64-v8a`. `-PsdlDir=/path/to/SDL` uses an SDL2 tree
elsewhere, `-PdecompRoot=/path/to/acww-decomp` another decomp checkout, and `-Pabis=x86_64`
builds for an emulator image. The release APK is signed with the debug key for now.

When a run cannot start on a phone, the app shows the end of its log; the whole of it is
`log.txt` in the app's files (`log-1.txt` is the run before).

## Tests

```sh
python3 port/tests/test_rom_prep.py build/acww-rom-prep
```

Builds synthetic DS ROMs (compressed ARM9, autoloads, plain and compressed overlays), splits each
with the port's C code and with the decomp's `tools/extract.py`, and compares the modules byte
for byte; it also checks the refusals (another release, not a ROM, no file). CI runs it, and
builds both platforms, on every push (`.github/workflows/build.yml`).

```sh
python3 port/tests/test_frames.py path/to/your.nds build
```

Plays the decomp's recordings in the build and compares the frames with the decomp's, bit for
bit. It needs the ROM, so it is run by hand; docs/64bit.md has the hosts it has passed on and
how to run it for ARM under qemu-user.

## Layout

| Path | What |
|---|---|
| `decomp/` | [acww-decomp](https://github.com/banatic/acww-decomp), as a submodule; built as it is |
| `port/CMakeLists.txt` | the decomp's runtime (the Reference form) and the front end, for Linux and Android |
| `port/src/front_sdl.c` | the SDL2 front end (the decomp's `runtime/play.c` on SDL2) |
| `port/src/touch.c` | the touch buttons and the screens' layout |
| `port/src/tall3d.c`, `gpu3d_main.c`, `gpu3d_upper.c` | the tall view: the decomp's 3D engine twice (docs/tall-view.md) |
| `port/src/sound_sdl.c` | sound: the decomp's `sound.c` with an SDL audio device |
| `port/src/game_hooks.c`, `run_port.c` | hooks into the game's code: drawing above the screen, Mr. Resetti |
| `port/src/rom_prep.c` | the ROM check and the split into modules |
| `port/src/native_os_portable.c` | stand-ins for the decomp's x86-only host code |
| `port/compat/` | headers force-included into the decomp's sources: Android's C library, 64-bit hosts |
| `port/tests/` | the ROM split against the decomp's (no ROM needed), the frames against the decomp's (your ROM) |
| `android/` | the Gradle project: `LaunchActivity` (asks for the ROM), `MainActivity` (SDL), `SettingsActivity` |
