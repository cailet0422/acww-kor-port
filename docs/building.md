<p align="center"><b>English</b> · <a href="building.ko.md">한국어</a></p>

# Building

Clone with the decomp:

```sh
git clone --recursive https://github.com/cailet0422/acww-kor-port
# or, in a clone made without it:
git submodule update --init
```

No ROM is needed to build; the program asks for it when it starts.

## Linux (32-bit x86)

The decomp's runtime is 32-bit (docs/roadmap.md), so on a 64-bit machine the program is built
with `-m32` against the 32-bit SDL2. On Ubuntu 24.04:

```sh
sudo dpkg --add-architecture i386
sudo apt-get update
sudo apt-get install gcc-multilib g++-multilib ninja-build libsdl2-dev:i386
cmake -S port -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=$PWD/port/cmake/linux-i686.cmake
cmake --build build
build/acww-kor --rom path/to/your.nds
```

`build/acww-kor --help` lists the options. `-DSDL_SOURCE_DIR=/path/to/SDL` builds SDL2 from a
source tree instead of using the system's.

## Android

Needs JDK 17, the Android SDK with platform 34, NDK `27.2.12479018` and CMake `3.22.1`
(`sdkmanager "platforms;android-34" "ndk;27.2.12479018" "cmake;3.22.1"`), and the SDL2 source:

```sh
git clone --depth 1 --branch release-2.30.9 https://github.com/libsdl-org/SDL.git android/SDL
cd android
./gradlew assembleRelease      # app/build/outputs/apk/release/app-release.apk
```

`-PsdlDir=/path/to/SDL` uses an SDL2 tree elsewhere, `-PdecompRoot=/path/to/acww-decomp` another
decomp checkout, and `-Pabis=x86` builds for a 32-bit x86 emulator image. The release APK is
signed with the debug key for now.

## Tests

```sh
python3 port/tests/test_rom_prep.py build/acww-rom-prep
```

Builds synthetic DS ROMs (compressed ARM9, autoloads, plain and compressed overlays), splits each
with the port's C code and with the decomp's `tools/extract.py`, and compares the modules byte
for byte; it also checks the refusals (another release, not a ROM, no file). CI runs it, and
builds both platforms, on every push (`.github/workflows/build.yml`).

## Layout

| Path | What |
|---|---|
| `decomp/` | [acww-decomp](https://github.com/banatic/acww-decomp), as a submodule; built as it is |
| `port/CMakeLists.txt` | the decomp's runtime (the Reference form) and the front end, for Linux and Android |
| `port/src/front_sdl.c` | the SDL2 front end (the decomp's `runtime/play.c` on SDL2) |
| `port/src/touch.c` | the touch buttons and the screens' layout |
| `port/src/rom_prep.c` | the ROM check and the split into modules |
| `port/src/native_os_portable.c` | stand-ins for the decomp's x86-only host code |
| `port/compat/port_compat.h` | force-included into the decomp's sources: Android's C library |
| `android/` | the Gradle project: `LaunchActivity` (asks for the ROM), `MainActivity` (SDL) |
