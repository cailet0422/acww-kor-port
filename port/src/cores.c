/* The fast cores of a phone (cores.h). */
#define _GNU_SOURCE
#include "cores.h"

#include <stdio.h>

#ifdef __linux__
#include <sched.h>
#include <unistd.h>

#define CORES_MAX 64

/* A number the system says about a core, or -1. */
static long core_number(int core, const char *what)
{
    char path[128];
    long value = -1;
    snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/%s", core, what);
    FILE *f = fopen(path, "r");
    if (f != NULL) {
        if (fscanf(f, "%ld", &value) != 1) {
            value = -1;
        }
        fclose(f);
    }
    return value;
}

int cores_prefer_fast(char *note, size_t size)
{
    long power[CORES_MAX], most = 0;
    int count = (int) sysconf(_SC_NPROCESSORS_CONF), fast = 0;
    cpu_set_t allowed, chosen;
    note[0] = 0;
    if (count < 2 || sched_getaffinity(0, sizeof(allowed), &allowed) != 0) {
        return 0;
    }
    count = count > CORES_MAX ? CORES_MAX : count;
    /* what a core can do: its capacity as the scheduler rates it (1024 for the fastest),
       or, where that is not told, its highest clock rate */
    for (int i = 0; i < count; i++) {
        power[i] = core_number(i, "cpu_capacity");
        if (power[i] <= 0) {
            power[i] = -1;
        }
    }
    for (int i = 0; i < count; i++) {
        if (power[i] < 0) {
            for (int k = 0; k < count; k++) { /* one core without a capacity: clock rates for all */
                power[k] = core_number(k, "cpufreq/cpuinfo_max_freq");
            }
            break;
        }
    }
    for (int i = 0; i < count; i++) {
        most = power[i] > most ? power[i] : most;
    }
    if (most <= 0) {
        return 0;
    }
    /* the cores within reach of the fastest; the slow ones of a phone are a third or a half */
    CPU_ZERO(&chosen);
    for (int i = 0; i < count; i++) {
        if (CPU_ISSET(i, &allowed) && power[i] * 100 >= most * 60) {
            CPU_SET(i, &chosen);
            fast++;
        }
    }
    if (fast < 2 || fast == CPU_COUNT(&allowed)) {
        return 0; /* all alike, or too few to hold the threads */
    }
    if (sched_setaffinity(0, sizeof(chosen), &chosen) != 0) {
        return 0;
    }
    int at = snprintf(note, size, "%d of %d cores:", fast, count);
    for (int i = 0; i < count && at > 0 && (size_t) at < size; i++) {
        if (CPU_ISSET(i, &chosen)) {
            at += snprintf(note + at, size - (size_t) at, " %d", i);
        }
    }
    return fast;
}

#else

int cores_prefer_fast(char *note, size_t size)
{
    if (size > 0) {
        note[0] = 0;
    }
    return 0;
}

#endif
