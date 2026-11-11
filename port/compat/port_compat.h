/* Force-included (-include) into every acww-decomp runtime source the port builds, so the
 * decomp itself stays untouched. It fills the few gaps between glibc, which the runtime is
 * written against, and the hosts the port adds:
 *
 *   - Android's C library (bionic) declares fseeko/ftello and the mmap flags only under
 *     _GNU_SOURCE (it does not know _DEFAULT_SOURCE), and memfd_create only from API 30.
 *     Before that the system call is made directly, and on kernels older than 3.17, which
 *     do not have it, an unlinked file in $TMPDIR (the app's cache, set by the front end)
 *     stands in: the runtime only needs a descriptor it can map twice (runtime/memory.c).
 */
#ifndef ACWW_PORT_COMPAT_H
#define ACWW_PORT_COMPAT_H

#if !defined(_WIN32) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif

#if defined(__ANDROID__) && __ANDROID_API__ < 30
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/syscall.h>
#include <unistd.h>

static inline int port_memfd_create(const char *name, unsigned int flags)
{
#ifdef __NR_memfd_create
    int fd = (int) syscall(__NR_memfd_create, name, flags);
    if (fd >= 0 || errno != ENOSYS) {
        return fd;
    }
#endif
    (void) name;
    (void) flags;
    const char *dir = getenv("TMPDIR");
    char path[512];
    snprintf(path, sizeof(path), "%s/acww-ram-XXXXXX", dir != NULL && dir[0] != 0 ? dir : "/data/local/tmp");
    int fd2 = mkstemp(path);
    if (fd2 >= 0) {
        unlink(path);
    }
    return fd2;
}
#define memfd_create port_memfd_create
#endif

#endif
