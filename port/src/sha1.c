#include "sha1.h"

#include <stdio.h>
#include <string.h>

static uint32_t rol(uint32_t x, int n)
{
    return (x << n) | (x >> (32 - n));
}

static void compress(PortSha1 *s, const uint8_t *p)
{
    uint32_t w[80];
    for (int i = 0; i < 16; i++) {
        w[i] = (uint32_t) p[4 * i] << 24 | (uint32_t) p[4 * i + 1] << 16 | (uint32_t) p[4 * i + 2] << 8 | p[4 * i + 3];
    }
    for (int i = 16; i < 80; i++) {
        w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    }
    uint32_t a = s->h[0], b = s->h[1], c = s->h[2], d = s->h[3], e = s->h[4];
    for (int i = 0; i < 80; i++) {
        uint32_t f, k;
        if (i < 20) {
            f = (b & c) | (~b & d);
            k = 0x5a827999u;
        } else if (i < 40) {
            f = b ^ c ^ d;
            k = 0x6ed9eba1u;
        } else if (i < 60) {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8f1bbcdcu;
        } else {
            f = b ^ c ^ d;
            k = 0xca62c1d6u;
        }
        uint32_t t = rol(a, 5) + f + e + k + w[i];
        e = d;
        d = c;
        c = rol(b, 30);
        b = a;
        a = t;
    }
    s->h[0] += a;
    s->h[1] += b;
    s->h[2] += c;
    s->h[3] += d;
    s->h[4] += e;
}

void port_sha1_init(PortSha1 *s)
{
    static const uint32_t start[5] = {0x67452301u, 0xefcdab89u, 0x98badcfeu, 0x10325476u, 0xc3d2e1f0u};
    memcpy(s->h, start, sizeof(start));
    s->length = 0;
    s->used = 0;
}

void port_sha1_update(PortSha1 *s, const void *data, size_t size)
{
    const uint8_t *p = data;
    s->length += size;
    if (s->used != 0) {
        size_t n = 64 - s->used < size ? 64 - s->used : size;
        memcpy(s->block + s->used, p, n);
        s->used += n;
        p += n;
        size -= n;
        if (s->used < 64) {
            return;
        }
        compress(s, s->block);
        s->used = 0;
    }
    for (; size >= 64; p += 64, size -= 64) {
        compress(s, p);
    }
    memcpy(s->block, p, size);
    s->used = size;
}

void port_sha1_final(PortSha1 *s, uint8_t digest[20])
{
    uint64_t bits = s->length * 8;
    uint8_t pad = 0x80;
    port_sha1_update(s, &pad, 1);
    uint8_t zero = 0;
    while (s->used != 56) {
        port_sha1_update(s, &zero, 1);
    }
    uint8_t len[8];
    for (int i = 0; i < 8; i++) {
        len[i] = (uint8_t) (bits >> (56 - 8 * i));
    }
    port_sha1_update(s, len, 8);
    for (int i = 0; i < 5; i++) {
        digest[4 * i] = (uint8_t) (s->h[i] >> 24);
        digest[4 * i + 1] = (uint8_t) (s->h[i] >> 16);
        digest[4 * i + 2] = (uint8_t) (s->h[i] >> 8);
        digest[4 * i + 3] = (uint8_t) s->h[i];
    }
}

void port_sha1_hex(const uint8_t digest[20], char out[41])
{
    for (int i = 0; i < 20; i++) {
        snprintf(out + 2 * i, 3, "%02x", digest[i]);
    }
}
