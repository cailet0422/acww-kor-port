<p align="center"><b>English</b> · <a href="64bit.ko.md">한국어</a></p>

# 64-bit hosts and ARM

The decomp's runtime is written as a 32-bit x86 program. The port also builds it for 64-bit
hosts and for ARM, which is what phones are. This page says why that is sound, how it is
checked, and what is left out.

## Why it works

The runtime maps the DS's memory at the DS's own addresses (main RAM at `0x02000000`, the ITCM
at `0x01ff8000`, the DTCM at `0x027e0000`) and the interpreter uses a DS address as a pointer.
`runtime/memory.c` therefore refuses to build unless pointers are 32 bits wide.

What the interpreter needs, though, is only that those addresses can be mapped as they are,
and they are all below 4 GiB: a 64-bit process can map them as well as a 32-bit one. What a
64-bit process cannot do is the reverse, hold a *host* pointer in a DS word. Only the Native
and Clean forms do that (the matching sources compiled for the host, whose structures are the
DS's). The port runs the **Reference form**, every function as the ROM's code in the
interpreter, and that form compiles for 64 bits without a warning.

So on 64-bit hosts the port answers `memory.c`'s check instead of changing the decomp
(`port/compat/memory_64bit.h`, force-included into that one file), and replaces the decomp's
x86-only host code with stand-ins that keep the Reference form's two needs: reserving the DS's
address ranges, and reporting a crash (`port/src/native_os_portable.c`).

## How it is checked

The decomp ships input recordings with the SHA-1 of the frames each must produce
(`decomp/runtime/recordings/`), and checks itself with them on a 32-bit x86 PC.
`port/tests/test_frames.py` plays the same recordings in a build of the port and compares:

```sh
python3 port/tests/test_frames.py your.nds build                # this machine
python3 port/tests/test_frames.py your.nds build-aarch64 --run "qemu-aarch64 -L /usr/aarch64-linux-gnu"
```

| Host | `title-start` (1,300 frames) | `new-game` (13,760 frames) |
|---|---|---|
| x86, 32-bit (the decomp's own host code and block translator) | same frames | same frames |
| x86-64 | same frames | same frames |
| ARM, 64-bit (`aarch64`, under qemu-user) | same frames | same frames |
| ARM, 32-bit (`armhf`, under qemu-user) | same frames | same frames |

"Same frames" is bit for bit: both screens of every frame. The ARM builds are Linux programs
made with the cross compilers (`port/cmake/linux-aarch64.cmake`, `linux-armhf.cmake`,
`-DPORT_FRONT_END=OFF`); the Android libraries are the same sources built with the NDK.

## What is different on a phone

- **The address ranges must be free in the app's process.** An Android app starts with the
  Java runtime's memory already mapped. In the one system tried so far, the Android 14
  emulator (an x86-64 image, the app as a 64-bit process), the DS's addresses were free and the
  game booted and ran; nothing promises that of every phone. If a run stops with `cannot map
  the DS memory`, that is this: the message and `log.txt` in the app's files say so.
- **Speed.** There is no block translator for ARM (the decomp's is x86 code), so every
  instruction is interpreted.

## What is left out

The Native and Clean forms: they need 32-bit pointers and, as written, x86 (docs/roadmap.md).
