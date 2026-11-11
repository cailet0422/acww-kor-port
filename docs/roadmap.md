<p align="center"><b>English</b> · <a href="roadmap.ko.md">한국어</a></p>

# Roadmap

What the port does now, what stands between it and a game that plays well on any phone, and the
order the work goes in. The facts about the decomp are from `decomp/` at the pinned commit
(`docs/runtime.md`, `docs/play.md` and the runtime's sources there).

## Where it stands (0.1.0)

| | State |
|---|---|
| Linux, 32-bit x86 | builds and links (CI); refuses a missing or wrong ROM with the reason |
| Android, `armeabi-v7a` | the APK builds (CI); **not yet run on a device** |
| ROM check and split | the same modules as the decomp's `tools/extract.py`, byte for byte, on synthetic ROMs (`port/tests/test_rom_prep.py`); the SHA-1 is checked against Python's |
| The game itself | untested on the port: the build environment has neither the ROM nor a phone |
| Sound | none (below) |
| 64-bit phones | cannot install it (below) |

## What the decomp's runtime assumes

1. **A 32-bit process.** `runtime/memory.c` stops the build on anything else (`#error "the
   runtime is a 32-bit program: DS addresses are host pointers"`): the DS's main RAM, ITCM and
   DTCM are mapped at the DS's own addresses (`0x02000000`, ...) and the interpreter uses DS
   addresses as pointers. So the port builds for `armeabi-v7a` (and `x86` for an emulator) only.
   Many phones released since 2023 run only 64-bit apps and cannot install it.
2. **x86 for the compiled forms.** The Native and Clean forms call compiled functions with x86
   conventions (`runtime/host.c` stops on other hosts), catch calls into DS code through x86
   fault frames (`REG_EIP` in `runtime/native_os.c`), patch x86 jumps into code, and enter the
   interpreter through x86 assembly (`runtime/native_tramp.S`). The port replaces those files
   with stand-ins that only keep the Reference form working.
3. **The interpreter's speed-up is x86 code.** Hot blocks are translated to x86 code
   (`runtime/arm_x86.c`, only on `__i386__`). On ARM every instruction is interpreted.
4. **Win32 for the window and sound.** `runtime/play.c` (the window) and the device part of
   `runtime/sound.c` (`waveOut`) are Windows-only; the mixer itself is not.
5. **glibc details.** `memfd_create` (in Android's C library only from API 30) and
   `fseeko`/`ftello` under `_DEFAULT_SOURCE`: `port/compat/port_compat.h` covers both.

## Next

1. **Run it on phones.** Check that the DS's addresses can be mapped in a 32-bit Android app
   (they are low addresses Android does not normally use, but nothing promises it), measure
   the interpreter's speed on ARM, and fix what breaks. Then tune the touch buttons, and let the
   player import and export the save and change the ROM from the app.
2. **Sound.** An SDL audio device for the decomp's mixer (`runtime/sound.h`, `runtime/ds_audio.h`),
   in place of the `waveOut` part.
3. **Speed, if the interpreter is too slow on phones.** In order of effort: compiler flags and
   profiling; an ARM version of the block translator (`arm_x86.c` shows the shape); the Native
   and Clean forms on ARM, which need an ARM `host_invoke` (AAPCS calls), ARM fault frames, and
   fibers without `makecontext` (Android's C library has none).
4. **64-bit phones.** The large one. The Reference form needs the interpreter's memory accesses
   to go through a base address instead of using DS addresses as host pointers. The compiled
   forms are harder: the decomp's structures hold 32-bit DS pointers. This needs the decomp's
   maintainers, as the decomp's own docs keep porting work in a separate project.
5. **Upstream.** Small changes to the decomp that let its runtime build on non-x86 hosts as it
   is (guards around `host.c`, `native_tramp.S` and the x86 fault code), so the port's stand-ins
   shrink.
