/* A phone has fast cores and slow ones, and the system may put a thread on either. The
 * emulation and the drawing each need a fast one to keep the DS's pace, so the thread that
 * calls this, and every thread it starts afterwards, is kept to the fast cores: those the
 * system rates within reach of its fastest. Nothing is done where the cores are all alike
 * (a desktop), where fewer than two are fast, or where the system does not say.
 */
#ifndef ACWW_PORT_CORES_H
#define ACWW_PORT_CORES_H

#include <stddef.h>

/* Returns how many cores the thread is kept to, with a word on which in `note`; 0 when
 * nothing was changed. */
int cores_prefer_fast(char *note, size_t size);

#endif
