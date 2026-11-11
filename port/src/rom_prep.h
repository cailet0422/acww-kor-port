/* The game's code from the player's own ROM: the check that it is the one the decomp is built
 * from, and the ARM9 modules (the main program, its autoloads and the overlays) written out
 * as the runtime's loader reads them (runtime/loader.c: one "name base path" line each).
 *
 * The same split as the decomp's tools/extract.py, done on the device, so the port needs no
 * Python and ships nothing from the ROM. */
#ifndef ACWW_PORT_ROM_PREP_H
#define ACWW_PORT_ROM_PREP_H

#include <stddef.h>

/* The Korean release (ADMK), the decomp's reference version (decomp/adm-kr.sha1), and what
 * decomp/tools/versions.py knows of it: the header's used size and the SHA-1 of a dump trimmed
 * to it. The cartridge is 256 Mbit. */
#define ROM_SHA1_ADMK "1fffd601d3439e37cd934e72d08e487a93fc449b"
#define ROM_USED_SIZE_ADMK 0x1F533C4u
#define ROM_TRIMMED_SHA1_ADMK "f987baf1db78b9b8c92a98877c62df71f9d049d3"
#define ROM_FULL_SIZE (32u << 20)

typedef enum RomStatus {
    ROM_OK,
    ROM_MISSING,       /* no file at the path */
    ROM_UNREADABLE,    /* the file cannot be read */
    ROM_NOT_DS,        /* not a DS ROM, or one whose layout does not make sense */
    ROM_OTHER_VERSION, /* a DS ROM, but another game, region or revision */
    ROM_BAD_DUMP,      /* the Korean release's header, other bytes (a bad dump or a changed ROM) */
    ROM_WRITE_FAILED,  /* the modules could not be written */
} RomStatus;

enum {
    ROM_PREP_NO_CHECK = 1, /* skip the SHA-1 check (the tests' synthetic ROMs) */
};

/* Checks `rom` and, unless `dir` already holds the modules of this ROM, writes them there with
 * dir/modules.txt. `rom_used` receives the ROM the run is to read (`rom` itself, or for a
 * trimmed dump the padded copy written to dir/padded.nds); `manifest` the path of
 * modules.txt; `detail`, on failure, what was wrong in a line for the player (the game code
 * found, the SHA-1, ...). */
RomStatus rom_prepare(const char *rom, const char *dir, int flags, char *rom_used, size_t rom_used_size, char *manifest,
                      size_t manifest_size, char *detail, size_t detail_size);

#endif
