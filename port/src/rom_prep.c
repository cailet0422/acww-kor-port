#include "rom_prep.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "sha1.h"

#define MAX_MODULES 160

typedef struct Module {
    char name[16];
    uint32_t base;
    const uint8_t *data;
    size_t size;
} Module;

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t) p[0] | (uint32_t) p[1] << 8 | (uint32_t) p[2] << 16 | (uint32_t) p[3] << 24;
}

static void wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t) v;
    p[1] = (uint8_t) (v >> 8);
    p[2] = (uint8_t) (v >> 16);
    p[3] = (uint8_t) (v >> 24);
}

/* Backward LZ, as NitroSDK's compstatic writes it. The last eight bytes describe the stream:
 * the header's length (top byte) and the compressed part's length (low 24 bits), then how much
 * longer the output is. The stream is read from its end towards its start, and the output is
 * written the same way; the bytes before the compressed part are kept as they are. Returns a
 * new buffer of *out_size bytes, or NULL if the stream does not fit its own description. */
static uint8_t *blz_decompress(const uint8_t *in, size_t size, size_t *out_size)
{
    if (size < 8) {
        return NULL;
    }
    uint32_t info = rd32(in + size - 8), extra = rd32(in + size - 4);
    uint8_t *out = malloc(size + extra);
    if (out == NULL) {
        return NULL;
    }
    memcpy(out, in, size);
    *out_size = size + extra;
    if (extra == 0) {
        return out;
    }
    size_t header = info >> 24, packed = info & 0xffffffu;
    if (header > size || packed > size || header > packed) {
        free(out);
        return NULL;
    }
    size_t src = size - header, dst = size + extra, stop = size - packed;
    while (src > stop) {
        uint8_t flags = in[--src];
        for (int bit = 0; bit < 8 && src > stop; bit++, flags = (uint8_t) (flags << 1)) {
            if (flags & 0x80) {
                if (src < stop + 2) {
                    free(out);
                    return NULL;
                }
                src -= 2;
                uint32_t pair = (uint32_t) in[src + 1] << 8 | in[src];
                size_t back = (pair & 0xfffu) + 3, count = (pair >> 12) + 3;
                if (count > dst || dst - count + back + count > size + extra) {
                    free(out);
                    return NULL;
                }
                while (count-- > 0) {
                    dst--;
                    out[dst] = out[dst + back];
                }
            } else {
                if (dst == 0) {
                    free(out);
                    return NULL;
                }
                out[--dst] = in[--src];
            }
        }
    }
    return out;
}

static uint8_t *read_file(const char *path, size_t *size, RomStatus *status)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        *status = errno == ENOENT ? ROM_MISSING : ROM_UNREADABLE;
        return NULL;
    }
    uint8_t *data = NULL;
    if (fseek(f, 0, SEEK_END) == 0) {
        long n = ftell(f);
        if (n > 0 && fseek(f, 0, SEEK_SET) == 0 && (data = malloc((size_t) n)) != NULL) {
            if (fread(data, 1, (size_t) n, f) == (size_t) n) {
                *size = (size_t) n;
            } else {
                free(data);
                data = NULL;
            }
        }
    }
    fclose(f);
    if (data == NULL) {
        *status = ROM_UNREADABLE;
    }
    return data;
}

static int write_file(const char *path, const void *data, size_t size)
{
    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        return -1;
    }
    int ok = (size == 0 || fwrite(data, 1, size, f) == size);
    return fclose(f) == 0 && ok ? 0 : -1;
}

static int same_text(const char *path, const char *text)
{
    char buf[64] = "";
    FILE *f = fopen(path, "r");
    if (f == NULL) {
        return 0;
    }
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = 0;
    buf[strcspn(buf, "\r\n")] = 0;
    return strcmp(buf, text) == 0;
}

/* The modules, as tools/extract.py's split() finds them. Buffers the split had to make
 * (decompressed images) are added to `owned` for the caller to free. */
static RomStatus split(uint8_t *rom, size_t rom_size, Module *mods, int *count, uint8_t **owned, int *owned_count,
                       char *detail, size_t detail_size)
{
#define BAD(...)                                     \
    do {                                             \
        snprintf(detail, detail_size, __VA_ARGS__);  \
        return ROM_NOT_DS;                           \
    } while (0)
    if (rom_size < 0x200) {
        BAD("the file is too small for a DS ROM (%zu bytes)", rom_size);
    }
    uint32_t arm9_off = rd32(rom + 0x20), arm9_ram = rd32(rom + 0x28), arm9_size = rd32(rom + 0x2c);
    uint32_t fat_off = rd32(rom + 0x48), ovt_off = rd32(rom + 0x50), ovt_size = rd32(rom + 0x54);
    if ((uint64_t) arm9_off + arm9_size > rom_size || (uint64_t) ovt_off + ovt_size > rom_size) {
        BAD("the header's ARM9 or overlay table is outside the file");
    }
    const uint8_t *arm9 = rom + arm9_off;
    size_t arm9_len = arm9_size;

    static const uint8_t magic[8] = {0x21, 0x06, 0xc0, 0xde, 0xde, 0xc0, 0x06, 0x21};
    size_t at = 0;
    while (at + sizeof(magic) <= arm9_len && memcmp(arm9 + at, magic, sizeof(magic)) != 0) {
        at++;
    }
    if (at + sizeof(magic) > arm9_len || at < 0x1c) {
        BAD("the ARM9 build information was not found");
    }
    size_t bi = at - 0x1c;
    uint32_t al_start = rd32(arm9 + bi), al_end = rd32(arm9 + bi + 4), al_data = rd32(arm9 + bi + 8);
    uint32_t bss_start = rd32(arm9 + bi + 12), bss_end = rd32(arm9 + bi + 16), comp_end = rd32(arm9 + bi + 20);
    (void) bss_start;
    (void) bss_end;
    if (comp_end != 0) {
        if (comp_end < arm9_ram || comp_end - arm9_ram > arm9_len) {
            BAD("the ARM9's compressed size is outside the image");
        }
        size_t n = comp_end - arm9_ram, unpacked_size;
        uint8_t *unpacked = blz_decompress(arm9, n, &unpacked_size);
        uint8_t *whole = unpacked == NULL ? NULL : malloc(unpacked_size + (arm9_len - n));
        if (whole == NULL) {
            free(unpacked);
            BAD("the ARM9 does not decompress");
        }
        memcpy(whole, unpacked, unpacked_size);
        memcpy(whole + unpacked_size, arm9 + n, arm9_len - n);
        free(unpacked);
        /* The SDK's decompressor clears this word once it has run: the image in RAM has 0. */
        wr32(whole + bi + 0x14, 0);
        owned[(*owned_count)++] = whole;
        arm9 = whole;
        arm9_len = unpacked_size + (arm9_len - n);
    }
    if (al_data < arm9_ram || al_data - arm9_ram > arm9_len || al_start < arm9_ram || al_end < al_start ||
        al_end - arm9_ram > arm9_len) {
        BAD("the ARM9's autoload table is outside the image");
    }

    int n = 0;
    size_t main_end = al_data - arm9_ram;
    snprintf(mods[n].name, sizeof(mods[n].name), "main");
    mods[n].base = arm9_ram;
    mods[n].data = arm9;
    mods[n].size = main_end;
    n++;

    size_t cursor = main_end;
    int unknown = 2;
    for (uint32_t k = 0; k < (al_end - al_start) / 12 && n < MAX_MODULES; k++) {
        const uint8_t *entry = arm9 + (al_start - arm9_ram) + 12 * k;
        uint32_t base = rd32(entry), size = rd32(entry + 4);
        if (cursor + size > arm9_len) {
            BAD("autoload %u is outside the image", k);
        }
        if (base == 0x01ff8000u) {
            snprintf(mods[n].name, sizeof(mods[n].name), "itcm");
        } else if (base == 0x027e0000u) {
            snprintf(mods[n].name, sizeof(mods[n].name), "dtcm");
        } else {
            snprintf(mods[n].name, sizeof(mods[n].name), "autoload_%d", unknown++);
        }
        mods[n].base = base;
        mods[n].data = arm9 + cursor;
        mods[n].size = size;
        cursor += size;
        n++;
    }

    for (uint32_t k = 0; k < ovt_size / 32 && n < MAX_MODULES; k++) {
        const uint8_t *entry = rom + ovt_off + 32 * k;
        uint32_t id = rd32(entry), ram = rd32(entry + 4), size = rd32(entry + 8), file_id = rd32(entry + 24);
        uint32_t flags = rd32(entry + 28);
        if ((uint64_t) fat_off + 8ull * file_id + 8 > rom_size) {
            BAD("overlay %u's file is outside the file table", id);
        }
        uint32_t start = rd32(rom + fat_off + 8 * file_id), end = rd32(rom + fat_off + 8 * file_id + 4);
        if (end < start || end > rom_size) {
            BAD("overlay %u's file is outside the ROM", id);
        }
        const uint8_t *data = rom + start;
        size_t len = end - start;
        if ((flags >> 24) & 1) {
            size_t packed = flags & 0xffffffu, unpacked_size;
            if (packed > len) {
                BAD("overlay %u's compressed size is larger than its file", id);
            }
            uint8_t *unpacked = blz_decompress(data, packed, &unpacked_size);
            if (unpacked == NULL) {
                BAD("overlay %u does not decompress", id);
            }
            owned[(*owned_count)++] = unpacked;
            data = unpacked;
            len = unpacked_size;
        }
        snprintf(mods[n].name, sizeof(mods[n].name), "ov%03u", id);
        mods[n].base = ram;
        mods[n].data = data;
        mods[n].size = size < len ? size : len;
        n++;
    }
    *count = n;
    return ROM_OK;
#undef BAD
}

static void describe_version(const uint8_t *rom, size_t size, const char *sha, char *detail, size_t detail_size)
{
    char code[5] = "????";
    if (size >= 0x10) {
        for (int i = 0; i < 4; i++) {
            code[i] = rom[0x0c + i] >= 0x20 && rom[0x0c + i] < 0x7f ? (char) rom[0x0c + i] : '?';
        }
    }
    const char *what = strcmp(code, "ADME") == 0   ? "the North American release"
                       : strcmp(code, "ADMJ") == 0 ? "the Japanese release"
                       : strcmp(code, "ADMP") == 0 ? "the European release"
                       : strcmp(code, "ADMK") == 0 ? "the Korean release, but not the dump the decomp checks"
                                                   : "another game";
    snprintf(detail, detail_size, "game code %s (%s), SHA-1 %s; this port needs ADMK, SHA-1 %s", code, what, sha,
             ROM_SHA1_ADMK);
}

static void sha1_of(const uint8_t *data, size_t size, char hex[41])
{
    PortSha1 s;
    uint8_t digest[20];
    port_sha1_init(&s);
    port_sha1_update(&s, data, size);
    port_sha1_final(&s, digest);
    port_sha1_hex(digest, hex);
}

RomStatus rom_prepare(const char *rom_path, const char *dir, int flags, char *rom_used, size_t rom_used_size,
                      char *manifest, size_t manifest_size, char *detail, size_t detail_size)
{
    detail[0] = 0;
    snprintf(manifest, manifest_size, "%s/modules.txt", dir);
    snprintf(rom_used, rom_used_size, "%s", rom_path);

    RomStatus status = ROM_OK;
    size_t size = 0;
    uint8_t *rom = read_file(rom_path, &size, &status);
    if (rom == NULL) {
        snprintf(detail, detail_size, "%s: %s", rom_path, status == ROM_MISSING ? "no such file" : "cannot be read");
        return status;
    }

    char sha[41];
    sha1_of(rom, size, sha);
    int padded = 0;
    if (!(flags & ROM_PREP_NO_CHECK) && strcmp(sha, ROM_SHA1_ADMK) != 0) {
        /* A dump trimmed to the used size, or with other padding, is padded back with 0xff as
           the cartridge reads (decomp/tools/setup.py) */
        char used[41] = "";
        if (size >= ROM_USED_SIZE_ADMK) {
            sha1_of(rom, ROM_USED_SIZE_ADMK, used);
        }
        uint8_t *full = strcmp(used, ROM_TRIMMED_SHA1_ADMK) == 0 ? realloc(rom, ROM_FULL_SIZE) : NULL;
        if (full != NULL) {
            rom = full;
            memset(rom + ROM_USED_SIZE_ADMK, 0xff, ROM_FULL_SIZE - ROM_USED_SIZE_ADMK);
            size = ROM_FULL_SIZE;
            sha1_of(rom, size, sha);
            padded = strcmp(sha, ROM_SHA1_ADMK) == 0;
        }
        if (!padded) {
            describe_version(rom, size, sha, detail, detail_size);
            int korean = size >= 0x10 && memcmp(rom + 0x0c, "ADMK", 4) == 0;
            free(rom);
            return korean ? ROM_BAD_DUMP : ROM_OTHER_VERSION;
        }
    }
    mkdir(dir, 0700);
    if (padded) {
        /* the run reads the ROM's files from the ROM itself: it gets the padded copy, and the
           player's file is left as it is */
        snprintf(rom_used, rom_used_size, "%s/padded.nds", dir);
        struct stat st;
        if ((stat(rom_used, &st) != 0 || (size_t) st.st_size != size) && write_file(rom_used, rom, size) != 0) {
            snprintf(detail, detail_size, "cannot write %s", rom_used);
            free(rom);
            return ROM_WRITE_FAILED;
        }
    }

    char stamp[1024];
    snprintf(stamp, sizeof(stamp), "%s/rom.sha1", dir);
    struct stat st;
    if (same_text(stamp, sha) && stat(manifest, &st) == 0) {
        free(rom);
        return ROM_OK;
    }

    Module mods[MAX_MODULES];
    uint8_t *owned[MAX_MODULES + 1];
    int count = 0, owned_count = 0;
    status = split(rom, size, mods, &count, owned, &owned_count, detail, detail_size);
    if (status == ROM_OK) {
        remove(stamp);
        FILE *list = fopen(manifest, "w");
        for (int i = 0; list != NULL && i < count; i++) {
            /* the loader reads paths of up to 511 characters (decomp/runtime/loader.c) */
            char path[512];
            int n = snprintf(path, sizeof(path), "%s/%s.bin", dir, mods[i].name);
            if (n < 0 || (size_t) n >= sizeof(path)) {
                snprintf(detail, detail_size, "the data directory's path is too long (%s)", dir);
                status = ROM_WRITE_FAILED;
                break;
            }
            if (write_file(path, mods[i].data, mods[i].size) != 0) {
                snprintf(detail, detail_size, "cannot write %s", path);
                status = ROM_WRITE_FAILED;
                break;
            }
            fprintf(list, "%s %08x %s\n", mods[i].name, mods[i].base, path);
        }
        if (list == NULL || fclose(list) != 0) {
            if (status == ROM_OK) {
                snprintf(detail, detail_size, "cannot write %s", manifest);
            }
            status = ROM_WRITE_FAILED;
        }
        if (status == ROM_OK && write_file(stamp, sha, strlen(sha)) != 0) {
            snprintf(detail, detail_size, "cannot write %s", stamp);
            status = ROM_WRITE_FAILED;
        }
    }
    for (int i = 0; i < owned_count; i++) {
        free(owned[i]);
    }
    free(rom);
    return status;
}
