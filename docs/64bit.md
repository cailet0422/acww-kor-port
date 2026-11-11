<p align="center"><b>English</b> · <a href="64bit.ko.md">한국어</a></p>

# 64-bit hosts and ARM

The decomp's runtime is written as a 32-bit x86 program. The port also builds it for 64-bit
hosts and for ARM, which is what phones are. This page says why that is sound, how it is
checked, and what is left out.

## Why it works

The runtime maps the DS's memory at the DS's own addresses (main RAM at `0x02000000`, the ITCM
at `0x01ff8000`, the DTCM at `0x027e0000`) and uses a DS address as a pointer.
`runtime/memory.c` therefore refuses to build unless pointers are 32 bits wide, and needs
those low addresses to be free.

Neither holds on a phone. Pointers are 64 bits wide, and the low addresses are not free: an
Android app starts with the Java runtime's memory already mapped, and on ARM that includes
the very range the DS's memory would take (0.5.0 stopped there on a real phone, "cannot map
the DS memory"; the x86-64 emulator, whose Java runtime allocates elsewhere, had hidden it).

So everywhere but on 32-bit x86 Linux, where the decomp's own host code and block translator
are used, the port puts the DS's memory **where the system has room** and adds a base:

- `port/src/memory_based.c` takes one stretch of address space large enough for every DS
  address the regions cover and maps the regions inside it at their offsets. A DS address is a
  host pointer once `memory_base` is added;
- the runtime reaches the DS's memory through `memory_host` of `memory.h`, and in five places
  in `arm.c` by using an address as a pointer. The build makes its own `memory.h` and `arm.c`
  from the decomp's, with the base added (`port/CMakeLists.txt`); a text it expects and does
  not find stops the build, so a change in the decomp cannot slip through;
- nothing else in the **Reference form** (every function as the ROM's code in the
  interpreter, which is what the port runs) takes a DS address for a pointer: it compiles
  for 64 bits without a warning, and the frames below are the proof. The Native and Clean
  forms, whose compiled code holds DS pointers in its structures, are not built;
- the decomp's x86-only host code is replaced by stand-ins
  (`port/src/native_os_portable.c`).

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

- **Speed.** There is no block translator for ARM (the decomp's is x86 code), so every
  instruction is interpreted, and every memory access adds the base.

## What is left out

The Native and Clean forms: they need 32-bit pointers and, as written, x86 (docs/roadmap.md).
