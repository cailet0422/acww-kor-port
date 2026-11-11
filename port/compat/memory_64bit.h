/* Force-included into decomp/runtime/memory.c on 64-bit hosts only.
 *
 * memory.c stops the build unless pointers are 32 bits wide ("the runtime is a 32-bit program:
 * DS addresses are host pointers"). What the runtime needs of the host is that the DS's
 * addresses, all below 4 GiB, can be mapped as they are, and that is as true of a 64-bit
 * process as of a 32-bit one: main RAM is at 0x02000000 in both. What a 64-bit process cannot
 * do is hold a *host* pointer in a DS word, which only the Native and Clean forms need (the
 * matching sources compiled for the host); the port runs the Reference form there, and
 * port/tests/test_frames.py checks its frames against the decomp's on such a host.
 *
 * So the check is answered here instead of in the decomp: UINTPTR_MAX is what memory.c
 * compares, and nothing else in that file reads it. */
#ifndef ACWW_PORT_MEMORY_64BIT_H
#define ACWW_PORT_MEMORY_64BIT_H

#include <stdint.h>

#undef UINTPTR_MAX
#define UINTPTR_MAX 0xffffffffu

#endif
