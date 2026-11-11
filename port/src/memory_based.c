/* The DS's memory at an address the system chooses (runtime/memory.h's functions, in place of
 * decomp/runtime/memory.c, on every host but 32-bit x86 Linux).
 *
 * The decomp maps the DS's regions at the DS's own addresses, so that a DS address is a host
 * pointer: main RAM at 0x02000000, and so on. Those are low addresses, and a process cannot
 * count on them: on ARM phones the Java runtime an app starts with has its own memory there
 * ("cannot map the DS memory"). So here one stretch of address space is taken wherever the
 * system has room, large enough for every DS address the regions cover, and the regions are
 * mapped inside it at their offsets: a DS address is a host pointer after memory_base is
 * added. The build gives the runtime's sources a memory.h whose memory_host adds it, and an
 * arm.c whose five direct uses of an address as a pointer add it (port/CMakeLists.txt makes
 * both from the decomp's files); everything else in the Reference form goes through
 * memory_host. The frames are checked to be the decomp's (port/tests/test_frames.py).
 */
#include "port_compat.h"

#include "memory.h"

#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

uint8_t *memory_base;

/* The regions a snapshot covers. The mirror is the same memory as the end of main RAM. */
static const struct {
    uint32_t base;
    uint32_t size;
} regions[] = {
    {MEM_ITCM_BASE, MEM_ITCM_SIZE},
    {MEM_MAIN_BASE, MEM_MAIN_SIZE},
    {MEM_DTCM_BASE, MEM_DTCM_SIZE},
};
#define REGION_COUNT (sizeof(regions) / sizeof(regions[0]))

/* every DS address below this is inside the stretch */
#define SPAN (MEM_MIRROR_BASE + MEM_MIRROR_SIZE)

static int anonymous(uint32_t base, uint32_t size)
{
    void *want = memory_base + base;
    return mmap(want, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0) == want ? 0 : -1;
}

/* Main RAM, mapped twice: at its address and, for its last 64 KiB, at the mirror. */
static int map_main(void)
{
    uint32_t offset = MEM_MIRROR_BASE - MEM_MAIN_MIRROR_STEP;
    int fd = memfd_create("ds-main-ram", 0);
    if (fd < 0 || ftruncate(fd, MEM_MAIN_SIZE) != 0) {
        return -1;
    }
    void *main = mmap(memory_base + MEM_MAIN_BASE, MEM_MAIN_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, fd, 0);
    void *mirror = mmap(memory_base + MEM_MIRROR_BASE, MEM_MIRROR_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, fd,
                        offset - MEM_MAIN_BASE);
    close(fd);
    return main == memory_base + MEM_MAIN_BASE && mirror == memory_base + MEM_MIRROR_BASE ? 0 : -1;
}

int memory_unshare(void)
{
    return 0; /* the port runs one console to a process and never forks */
}

int memory_init(void)
{
    static int done;
    if (done) {
        return 0;
    }
    /* the stretch: nothing in it can be touched but the regions mapped below */
    void *stretch = mmap(NULL, SPAN, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (stretch == MAP_FAILED) {
        return -1;
    }
    memory_base = stretch;
    if (anonymous(MEM_ITCM_BASE, MEM_ITCM_SIZE) != 0 || map_main() != 0 || anonymous(MEM_DTCM_BASE, MEM_DTCM_SIZE) != 0) {
        return -1;
    }
    for (size_t i = 0; i < REGION_COUNT; i++) {
        memset(memory_base + regions[i].base, 0, regions[i].size);
    }
    done = 1;
    return 0;
}

struct MemorySnapshot {
    uint8_t *copy[REGION_COUNT];
};

MemorySnapshot *memory_snapshot(void)
{
    MemorySnapshot *s = calloc(1, sizeof(*s));
    for (size_t i = 0; i < REGION_COUNT; i++) {
        s->copy[i] = malloc(regions[i].size);
        memcpy(s->copy[i], memory_base + regions[i].base, regions[i].size);
    }
    return s;
}

void memory_restore(const MemorySnapshot *s)
{
    for (size_t i = 0; i < REGION_COUNT; i++) {
        memcpy(memory_base + regions[i].base, s->copy[i], regions[i].size);
    }
}

void memory_snapshot_free(MemorySnapshot *s)
{
    for (size_t i = 0; i < REGION_COUNT; i++) {
        free(s->copy[i]);
    }
    free(s);
}

void memory_diff(const MemorySnapshot *before, uint32_t skip_lo, uint32_t skip_hi,
                 void (*found)(void *user, uint32_t address, const uint8_t *was, const uint8_t *now, size_t size), void *user)
{
    for (size_t i = 0; i < REGION_COUNT; i++) {
        const uint8_t *was = before->copy[i], *now = memory_base + regions[i].base;
        uint32_t n = regions[i].size;
        for (uint32_t off = 0; off < n;) {
            uint32_t address = regions[i].base + off;
            if (was[off] == now[off] || (address >= skip_lo && address < skip_hi)) {
                off++;
                continue;
            }
            uint32_t end = off;
            while (end < n && was[end] != now[end] && !(regions[i].base + end >= skip_lo && regions[i].base + end < skip_hi)) {
                end++;
            }
            found(user, address, was + off, now + off, end - off);
            off = end;
        }
    }
}
