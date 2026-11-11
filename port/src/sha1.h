/* SHA-1, for checking the ROM against the one the decomp is built from. */
#ifndef ACWW_PORT_SHA1_H
#define ACWW_PORT_SHA1_H

#include <stddef.h>
#include <stdint.h>

typedef struct PortSha1 {
    uint32_t h[5];
    uint64_t length;
    uint8_t block[64];
    size_t used;
} PortSha1;

void port_sha1_init(PortSha1 *s);
void port_sha1_update(PortSha1 *s, const void *data, size_t size);
void port_sha1_final(PortSha1 *s, uint8_t digest[20]);

/* digest as 40 lowercase hex digits and a terminating zero */
void port_sha1_hex(const uint8_t digest[20], char out[41]);

#endif
