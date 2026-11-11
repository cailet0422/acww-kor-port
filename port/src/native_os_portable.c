/* runtime/native_os.h, runtime/host.h and runtime/native_tramp.S for hosts that cannot run
 * the Native form: everything but desktop 32-bit x86 Linux and Windows (Android on ARM, and
 * Android on x86, whose C library has no makecontext/swapcontext).
 *
 * The decomp's versions run the matching sources compiled for the PC, which assumes a
 * 32-bit x86 host (cdecl calls, x86 fault frames, x86 jumps patched into code). The port
 * builds the Reference form there instead: every function is the ROM's code in the
 * interpreter (docs/roadmap.md). That form still asks for two things here, which are real:
 *
 *   - native_os_report_crashes: a line on stderr (the log file on Android) when the process
 *     faults;
 *   - native_os_reserve, which run.c calls before the run starts: nothing to do (below).
 *
 * The rest belongs to the Native run and reports that it is not available if anything ever
 * reaches it. */
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "host.h"
#include "native_os.h"

static void not_here(const char *what)
{
    fprintf(stderr, "port: %s is part of the Native form, which this host does not run\n", what);
    fflush(stderr);
    abort();
}

void native_trampoline(void)
{
    not_here("native_trampoline");
}

void native_call_stub(void)
{
    not_here("native_call_stub");
}

uint64_t host_invoke(const void *fn, const uint32_t *args, int count, int thiscall)
{
    (void) fn;
    (void) args;
    (void) count;
    (void) thiscall;
    not_here("host_invoke");
    return 0;
}

int host_guarded_invoke(const void *fn, const uint32_t *args, int count, int thiscall, uint64_t *result, uint32_t *fault)
{
    (void) fn;
    (void) args;
    (void) count;
    (void) thiscall;
    (void) result;
    (void) fault;
    not_here("host_guarded_invoke");
    return -1;
}

static void on_crash(int sig, siginfo_t *info, void *context)
{
    (void) context;
    fprintf(stderr, "crash: signal %d, address %08lx\n", sig, (unsigned long) (uintptr_t) info->si_addr);
    fflush(stderr);
    signal(sig, SIG_DFL);
    raise(sig);
}

void native_os_report_crashes(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = on_crash;
    sa.sa_flags = SA_SIGINFO | SA_RESETHAND;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGBUS, &sa, NULL);
    sigaction(SIGILL, &sa, NULL);
    sigaction(SIGFPE, &sa, NULL);
}

int native_os_init(void)
{
    fprintf(stderr, "port: the Native form is not available on this host\n");
    return -1;
}

int native_os_watchdog(int seconds, int (*progress)(void), void (*stuck)(uint32_t eip))
{
    (void) seconds;
    (void) progress;
    (void) stuck;
    return -1;
}

void *native_os_fiber_current(void)
{
    return NULL;
}

void *native_os_fiber_create(void (*entry)(void))
{
    (void) entry;
    return NULL;
}

void native_os_fiber_switch(void *fiber)
{
    (void) fiber;
    not_here("native_os_fiber_switch");
}

int native_os_patch(void *at, const void *bytes, size_t size)
{
    (void) at;
    (void) bytes;
    (void) size;
    return -1;
}

/* A write() of the bytes to a pipe fails with EFAULT where the process cannot read them. */
int native_os_accessible(uint32_t address, uint32_t size, int for_write)
{
    static int probe[2] = {-1, -1};
    (void) for_write;
    if (probe[0] < 0 && pipe(probe) != 0) {
        return 0;
    }
    char sink[64];
    for (uint32_t done = 0; done < size;) {
        size_t n = size - done < sizeof(sink) ? size - done : sizeof(sink);
        if (write(probe[1], (const void *) (uintptr_t) (address + done), n) != (ssize_t) n ||
            read(probe[0], sink, n) != (ssize_t) n) {
            return 0;
        }
        done += (uint32_t) n;
    }
    return 1;
}

void *native_os_code_alloc(size_t size)
{
    (void) size;
    return NULL;
}

/* The DS's address ranges need no keeping away from the host's allocations here: the DS's
 * memory is not at its own addresses but inside a stretch of its own (memory_based.c), and no
 * host pointer is ever taken for a DS address. */
uint32_t native_os_reserve(uint32_t base, uint32_t size)
{
    (void) base;
    (void) size;
    return 0;
}
