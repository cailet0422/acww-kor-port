/* acww-rom-prep: the front end's ROM check and split on their own (port/src/rom_prep.c).
 *
 *     acww-rom-prep [--no-check] ROM DIR
 *
 * Writes DIR/modules.txt and DIR/<module>.bin as the front end does at start, and prints the
 * ROM the run would read and the path of modules.txt. --no-check skips the SHA-1 check
 * (port/tests/test_rom_prep.py builds synthetic ROMs). */
#include <stdio.h>
#include <string.h>

#include "rom_prep.h"

int main(int argc, char **argv)
{
    int flags = 0, first = 1;
    if (argc > 1 && strcmp(argv[1], "--no-check") == 0) {
        flags |= ROM_PREP_NO_CHECK;
        first = 2;
    }
    if (argc - first != 2) {
        fprintf(stderr, "usage: acww-rom-prep [--no-check] ROM DIR\n");
        return 2;
    }
    char rom_used[1024], manifest[1024], detail[512];
    RomStatus status = rom_prepare(argv[first], argv[first + 1], flags, rom_used, sizeof(rom_used), manifest,
                                   sizeof(manifest), detail, sizeof(detail));
    if (status != ROM_OK) {
        fprintf(stderr, "acww-rom-prep: %s (status %d)\n", detail, (int) status);
        return 1;
    }
    printf("%s\n%s\n", rom_used, manifest);
    return 0;
}
