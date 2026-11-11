/* The Wi-Fi Connection gateway (port/src/wifi_port.c) without the game: a console's side made
 * here, packet by packet, as the game's own TCP/IP would send them, against servers on this
 * machine's loopback. Checked:
 *
 *   - the connection settings made in the firmware image: three pages with good checksums,
 *     the first set for the port's access point; pages kept from a run before are put back;
 *   - the access point is found by a scan, by name and without;
 *   - ARP for the router, DHCP's offer and acknowledgement;
 *   - a name query to the router reaches the chosen name server and its answer comes back
 *     as from the router;
 *   - UDP to an address and back;
 *   - TCP: a connection, 300000 bytes each way with a server that answers every byte
 *     changed, both ends' closing; a connection to a port nobody listens on is reset;
 *   - every packet to the console has good IP, UDP and TCP checksums.
 *
 *     acww-wifi-test
 */
#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "wifi.h"
#include "wifi_port.h"

#define ROUTER 0xc0a80b01u
#define CLIENT 0xc0a80b02u
#define LOOPBACK 0x7f000001u
#define STREAM 300000u
#define CYCLES_PER_MS 33514u

static const uint8_t LLC[6] = {0xaa, 0xaa, 0x03, 0x00, 0x00, 0x00};
static const uint8_t CONSOLE_MAC[6] = {0x00, 0x09, 0xbf, 0x12, 0x34, 0x56};

static uint64_t now_cycles;
static int failures;

uint32_t io_frame(void)
{
    return (uint32_t) (now_cycles / 560190u);
}

#define CHECK(what, ...)                           \
    do {                                           \
        if (!(what)) {                             \
            printf("wifi-test: FAILED: ");         \
            printf(__VA_ARGS__);                   \
            printf("\n");                          \
            failures++;                            \
        }                                          \
    } while (0)

static void be16(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t) (v >> 8);
    p[1] = (uint8_t) v;
}

static void be32(uint8_t *p, uint32_t v)
{
    be16(p, v >> 16);
    be16(p + 2, v);
}

static uint32_t get16(const uint8_t *p)
{
    return (uint32_t) p[0] << 8 | p[1];
}

static uint32_t get32(const uint8_t *p)
{
    return get16(p) << 16 | get16(p + 2);
}

static uint32_t sum_of(uint32_t sum, const uint8_t *p, uint32_t n)
{
    for (uint32_t i = 0; i + 1 < n; i += 2) {
        sum += get16(p + i);
    }
    if (n & 1) {
        sum += (uint32_t) p[n - 1] << 8;
    }
    return sum;
}

static uint32_t sum_fold(uint32_t sum)
{
    while (sum >> 16) {
        sum = (sum & 0xffff) + (sum >> 16);
    }
    return ~sum & 0xffff;
}

static uint32_t pseudo(uint32_t src, uint32_t dst, uint32_t proto, uint32_t length)
{
    return (src >> 16) + (src & 0xffff) + (dst >> 16) + (dst & 0xffff) + proto + length;
}

static uint32_t crc16(const uint8_t *p, uint32_t n)
{
    uint32_t crc = 0;
    for (uint32_t i = 0; i < n; i++) {
        crc ^= p[i];
        for (int k = 0; k < 8; k++) {
            crc = crc & 1 ? (crc >> 1) ^ 0xa001u : crc >> 1;
        }
    }
    return crc & 0xffff;
}

/* The console's side ---------------------------------------------------------------------------- */

static void send_frame(uint32_t ethertype, const uint8_t *payload, uint32_t length)
{
    uint8_t frame[WIFI_FRAME_MAX];
    static const uint8_t to[6] = {0x02, 0x00, 0x41, 0x43, 0x57, 0x02};
    memcpy(frame, LLC, 6);
    be16(frame + 6, ethertype);
    memcpy(frame + 8, payload, length);
    wifi_send(now_cycles, CONSOLE_MAC, to, frame, 8 + length);
}

static void send_ip(uint32_t proto, uint32_t src, uint32_t dst, const uint8_t *payload, uint32_t length)
{
    uint8_t p[WIFI_FRAME_MAX];
    static uint16_t id;
    memset(p, 0, 20);
    p[0] = 0x45;
    be16(p + 2, 20 + length);
    be16(p + 4, id++);
    p[8] = 64;
    p[9] = (uint8_t) proto;
    be32(p + 12, src);
    be32(p + 16, dst);
    be16(p + 10, sum_fold(sum_of(0, p, 20)));
    memcpy(p + 20, payload, length);
    send_frame(0x0800, p, 20 + length);
}

static void send_udp(uint32_t src, uint32_t sport, uint32_t dst, uint32_t dport, const uint8_t *data, uint32_t length)
{
    uint8_t d[WIFI_FRAME_MAX];
    be16(d, sport);
    be16(d + 2, dport);
    be16(d + 4, 8 + length);
    be16(d + 6, 0);
    memcpy(d + 8, data, length);
    be16(d + 6, sum_fold(sum_of(pseudo(src, dst, 17, 8 + length), d, 8 + length)));
    send_ip(17, src, dst, d, 8 + length);
}

static void send_tcp(uint32_t dst, uint32_t sport, uint32_t dport, uint32_t seq, uint32_t ack, uint32_t flags,
                     const uint8_t *data, uint32_t length)
{
    uint8_t s[WIFI_FRAME_MAX];
    memset(s, 0, 20);
    be16(s, sport);
    be16(s + 2, dport);
    be32(s + 4, seq);
    be32(s + 8, ack);
    s[12] = 0x50;
    s[13] = (uint8_t) flags;
    be16(s + 14, 4096);
    if (length != 0) {
        memcpy(s + 20, data, length);
    }
    be16(s + 16, sum_fold(sum_of(pseudo(CLIENT, dst, 6, 20 + length), s, 20 + length)));
    send_ip(6, CLIENT, dst, s, 20 + length);
}

/* The next frame for the console within `ms` of the DS's time (and as long of this machine's,
 * since its sockets take their own time), or 0. The packet's checksums are checked. */
static uint32_t receive(uint8_t *frame, uint32_t ms)
{
    for (uint32_t t = 0; t <= ms; t++) {
        uint8_t src[6], dst[6];
        uint32_t n = wifi_receive(now_cycles, src, dst, frame, WIFI_FRAME_MAX);
        if (n != 0) {
            now_cycles += CYCLES_PER_MS; /* a frame takes its time on the air */
            CHECK(n >= 8 && memcmp(frame, LLC, 6) == 0, "a frame without the LLC header");
            CHECK(memcmp(dst, CONSOLE_MAC, 6) == 0, "a frame for another console");
            if (get16(frame + 6) == 0x0800) {
                const uint8_t *p = frame + 8;
                uint32_t total = get16(p + 2), proto = p[9];
                CHECK(total == n - 8, "an IP packet of %u bytes in a frame of %u", total, n - 8);
                CHECK(sum_fold(sum_of(0, p, 20)) == 0, "an IP header's checksum");
                if (proto == 6 || proto == 17) {
                    CHECK(sum_fold(sum_of(pseudo(get32(p + 12), get32(p + 16), proto, total - 20), p + 20, total - 20)) == 0,
                          "a %s checksum", proto == 6 ? "TCP" : "UDP");
                }
            }
            return n;
        }
        now_cycles += CYCLES_PER_MS;
        struct timespec nap = {0, 1000000};
        nanosleep(&nap, NULL);
    }
    return 0;
}

/* This machine's servers ------------------------------------------------------------------------ */

static int listen_on(int type, uint16_t *port)
{
    int fd = socket(AF_INET, type, 0);
    struct sockaddr_in at;
    socklen_t size = sizeof(at);
    memset(&at, 0, sizeof(at));
    at.sin_family = AF_INET;
    at.sin_addr.s_addr = htonl(LOOPBACK);
    if (fd < 0 || bind(fd, (struct sockaddr *) &at, sizeof(at)) != 0 || getsockname(fd, (struct sockaddr *) &at, &size) != 0 ||
        (type == SOCK_STREAM && listen(fd, 4) != 0)) {
        printf("wifi-test: cannot open a socket on the loopback\n");
        exit(2);
    }
    *port = ntohs(at.sin_port);
    return fd;
}

/* UDP: what comes goes back with every byte + 1 (the name server and the echo are both this). */
static void *udp_server(void *arg)
{
    int fd = *(int *) arg;
    for (;;) {
        uint8_t data[2048];
        struct sockaddr_in from;
        socklen_t size = sizeof(from);
        ssize_t n = recvfrom(fd, data, sizeof(data), 0, (struct sockaddr *) &from, &size);
        if (n <= 0) {
            break;
        }
        for (ssize_t i = 0; i < n; i++) {
            data[i]++;
        }
        sendto(fd, data, (size_t) n, 0, (struct sockaddr *) &from, size);
    }
    return NULL;
}

/* TCP: every byte back with 0x5a on it, in pieces of odd sizes; the end after the client's. */
static void *tcp_server(void *arg)
{
    int fd = *(int *) arg;
    int c = accept(fd, NULL, NULL);
    uint8_t data[3001];
    for (;;) {
        ssize_t n = recv(c, data, sizeof(data), 0);
        if (n <= 0) {
            break;
        }
        for (ssize_t i = 0; i < n; i++) {
            data[i] ^= 0x5a;
        }
        for (ssize_t done = 0; done < n;) {
            ssize_t w = send(c, data + done, (size_t) (n - done), MSG_NOSIGNAL);
            if (w <= 0) {
                break;
            }
            done += w;
        }
    }
    close(c);
    return NULL;
}

static uint8_t byte_at(uint32_t i)
{
    return (uint8_t) (i * 7 + (i >> 8) * 13 + (i >> 16));
}

/* The checks ------------------------------------------------------------------------------------- */

static void check_settings(uint8_t *nvram)
{
    const uint8_t *s = wifi_port_settings();
    CHECK(s == nvram + WIFI_PORT_SETTINGS_AT, "the settings are not where the firmware image has them");
    for (int page = 0; page < 3; page++) {
        const uint8_t *p = s + 0x100 * page;
        CHECK(crc16(p, 0xfe) == (uint32_t) (p[0xfe] | p[0xff] << 8), "the checksum of connection %d", page + 1);
        CHECK(page == 0 ? p[0xe7] == 0 && memcmp(p + 0x40, "acww-kor-port", 13) == 0 : p[0xe7] == 0xff,
              "connection %d is not %s", page + 1, page == 0 ? "the access point's" : "left unset");
    }
}

static void check_scan(void)
{
    static const uint8_t any[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    uint8_t desc[WIFI_BSS_SIZE];
    CHECK(wifi_scan(0x3fff, any, NULL, 0, 0, desc) == 1, "a scan of every channel finds no access point");
    CHECK(desc[0xa] == 13 && memcmp(desc + 0xc, "acww-kor-port", 13) == 0, "the access point's name");
    CHECK(wifi_is_ap(desc + 4), "the access point's address is not known as its own");
    CHECK(wifi_scan(0x3fff, any, (const uint8_t *) "acww-kor-port", 13, 13, desc) == 1, "a scan by its name");
    CHECK(wifi_scan(0x3fff, any, (const uint8_t *) "another", 7, 7, desc) == 0, "a scan by another name found it");
}

static void check_arp_dhcp(void)
{
    uint8_t frame[WIFI_FRAME_MAX], arp[28], dhcp[300];
    memset(arp, 0, sizeof(arp));
    be16(arp, 1), be16(arp + 2, 0x0800), arp[4] = 6, arp[5] = 4, be16(arp + 6, 1);
    memcpy(arp + 8, CONSOLE_MAC, 6);
    be32(arp + 14, CLIENT);
    be32(arp + 24, ROUTER);
    send_frame(0x0806, arp, sizeof(arp));
    uint32_t n = receive(frame, 50);
    CHECK(n == 36 && get16(frame + 6) == 0x0806 && get16(frame + 8 + 6) == 2 && get32(frame + 8 + 14) == ROUTER,
          "no ARP answer for the router");

    for (int type = 1; type <= 3; type += 2) {
        memset(dhcp, 0, sizeof(dhcp));
        dhcp[0] = 1, dhcp[1] = 1, dhcp[2] = 6;
        be32(dhcp + 4, 0x12345678u);
        memcpy(dhcp + 28, CONSOLE_MAC, 6);
        be32(dhcp + 236, 0x63825363u);
        dhcp[240] = 53, dhcp[241] = 1, dhcp[242] = (uint8_t) type, dhcp[243] = 255;
        send_udp(0, 68, 0xffffffffu, 67, dhcp, sizeof(dhcp));
        n = receive(frame, 50);
        const uint8_t *r = frame + 8 + 28;
        CHECK(n >= 8 + 28 + 244 && r[0] == 2 && get32(r + 4) == 0x12345678u && get32(r + 16) == CLIENT &&
                  r[242] == (type == 1 ? 2 : 5),
              "DHCP: no %s", type == 1 ? "offer" : "acknowledgement");
        int router = 0, names = 0;
        for (uint32_t i = 240; n != 0 && i + 1 < n - 36 && r[i] != 255; i += 2u + r[i + 1]) {
            router |= r[i] == 3 && get32(r + i + 2) == ROUTER;
            names |= r[i] == 6 && get32(r + i + 2) == ROUTER;
        }
        CHECK(router && names, "DHCP: the router or the name server is not given");
    }
}

static void check_udp(uint16_t names_port, uint16_t echo_port)
{
    uint8_t frame[WIFI_FRAME_MAX], q[40];
    (void) names_port;
    for (uint32_t i = 0; i < sizeof(q); i++) {
        q[i] = (uint8_t) (i * 3);
    }
    /* a query to the router: answered by the chosen name server, as from the router */
    send_udp(CLIENT, 2000, ROUTER, 53, q, sizeof(q));
    uint32_t n = receive(frame, 2000);
    const uint8_t *p = frame + 8;
    int right = n == 8 + 28 + sizeof(q) && get32(p + 12) == ROUTER && get16(p + 20) == 53 && get16(p + 22) == 2000;
    for (uint32_t i = 0; right && i < sizeof(q); i++) {
        right = p[28 + i] == (uint8_t) (q[i] + 1);
    }
    CHECK(right, "the name server's answer did not come back as the router's");
    /* to an address, and back from it */
    send_udp(CLIENT, 2001, LOOPBACK, echo_port, q, sizeof(q));
    n = receive(frame, 2000);
    right = n == 8 + 28 + sizeof(q) && get32(p + 12) == LOOPBACK && get16(p + 20) == echo_port && get16(p + 22) == 2001;
    for (uint32_t i = 0; right && i < sizeof(q); i++) {
        right = p[28 + i] == (uint8_t) (q[i] + 1);
    }
    CHECK(right, "UDP to an address did not come back from it");
}

static void check_tcp(uint16_t port, uint16_t closed_port)
{
    uint8_t frame[WIFI_FRAME_MAX];
    const uint8_t *p = frame + 8, *t = frame + 8 + 20;
    uint32_t seq = 1000, ack = 0, n;

    /* nobody listens: reset */
    send_tcp(LOOPBACK, 3000, closed_port, 500, 0, 0x02, NULL, 0);
    n = receive(frame, 3000);
    CHECK(n != 0 && (t[13] & 0x04) && get32(t + 8) == 501, "a connection to a closed port is not reset");

    send_tcp(LOOPBACK, 3001, port, seq, 0, 0x02, NULL, 0);
    n = receive(frame, 3000);
    if (!(n != 0 && (t[13] & 0x12) == 0x12 && get32(t + 8) == seq + 1 && get32(p + 12) == LOOPBACK && get16(t) == port)) {
        CHECK(0, "TCP: no answer to the connection");
        return;
    }
    seq++;
    ack = get32(t + 4) + 1;
    send_tcp(LOOPBACK, 3001, port, seq, ack, 0x10, NULL, 0);

    /* both ways at once: a window of segments out, whatever comes taken and acknowledged;
       now and then a segment is sent twice and one is left out, as the air does */
    uint32_t sent = 0, acked = 0, got = 0, window = 4096, turns = 0, wrong = 0, their_fin = 0, our_fin = 0;
    uint64_t stalled = now_cycles;
    while ((got < STREAM || !their_fin) && turns < 4000000) {
        turns++;
        while (sent < STREAM && sent - acked + 1000 <= window) {
            uint8_t data[1000];
            uint32_t k = STREAM - sent < 1000 ? STREAM - sent : 1000;
            for (uint32_t i = 0; i < k; i++) {
                data[i] = byte_at(sent + i);
            }
            static uint8_t left_out[STREAM / 1000 + 1];
            if (sent / 1000 % 37 != 5 || left_out[sent / 1000]++ != 0) {
                send_tcp(LOOPBACK, 3001, port, seq + sent, ack, 0x18, data, k);
            }
            if (sent / 1000 % 23 == 7) {
                send_tcp(LOOPBACK, 3001, port, seq + sent, ack, 0x18, data, k);
            }
            sent += k;
        }
        if (sent == STREAM && acked == STREAM && !our_fin) {
            send_tcp(LOOPBACK, 3001, port, seq + STREAM, ack, 0x11, NULL, 0);
            our_fin = 1;
        }
        n = receive(frame, 20);
        if (n == 0) {
            if (now_cycles - stalled > 400ull * CYCLES_PER_MS) {
                if (getenv("WIFI_TEST_TRACE")) {
                    printf("stall: sent %u acked %u got %u window %u\n", sent, acked, got, window);
                }
                sent = acked; /* nothing for a while: from what was taken, again */
                our_fin = 0;
                stalled = now_cycles;
            }
            continue;
        }
        stalled = now_cycles;
        if (get16(frame + 6) != 0x0800 || p[9] != 6 || get16(t + 2) != 3001) {
            continue;
        }
        uint32_t flags = t[13], header = (uint32_t) (t[12] >> 4) * 4, length = get16(p + 2) - 20 - header;
        window = get16(t + 14);
        if (flags & 0x04) {
            CHECK(0, "TCP: reset after %u bytes out, %u back", acked, got);
            return;
        }
        if (flags & 0x10) {
            uint32_t a = get32(t + 8) - seq;
            if (a <= STREAM + 1 && a > acked) {
                acked = a > STREAM ? STREAM : a;
            }
        }
        if (get32(t + 4) == ack) {
            for (uint32_t i = 0; i < length; i++) {
                wrong += t[header + i] != (uint8_t) (byte_at(got + i) ^ 0x5a);
            }
            got += length;
            ack += length;
            if (flags & 0x01) {
                their_fin = 1;
                ack++;
            }
        }
        if (length != 0 || (flags & 0x01)) {
            send_tcp(LOOPBACK, 3001, port, seq + (our_fin ? STREAM + 1 : sent), ack, 0x10, NULL, 0);
        }
    }
    CHECK(acked == STREAM, "TCP: %u of %u bytes were taken", acked, STREAM);
    CHECK(got == STREAM && wrong == 0, "TCP: %u of %u bytes came back, %u of them wrong", got, STREAM, wrong);
    CHECK(their_fin, "TCP: the server's end did not come");
    printf("wifi-test: TCP: %u bytes each way in %u ms of the DS's time\n", STREAM, (unsigned) (now_cycles / CYCLES_PER_MS));
}

int main(void)
{
    static uint8_t nvram[0x40000];
    uint16_t names_port, echo_port, stream_port, closed_port;
    int names = listen_on(SOCK_DGRAM, &names_port), echo = listen_on(SOCK_DGRAM, &echo_port);
    int stream = listen_on(SOCK_STREAM, &stream_port), closed = listen_on(SOCK_STREAM, &closed_port);
    pthread_t threads[3];
    char dns[64];
    setvbuf(stdout, NULL, _IONBF, 0);
    close(closed); /* a port nobody listens on */
    pthread_create(&threads[0], NULL, udp_server, &names);
    pthread_create(&threads[1], NULL, udp_server, &echo);
    pthread_create(&threads[2], NULL, tcp_server, &stream);

    /* the firmware image: only where its user settings are (in units of 8 bytes, at 0x20) */
    memset(nvram, 0xff, sizeof(nvram));
    nvram[0x20] = 0xc0;
    nvram[0x21] = 0x7f;
    snprintf(dns, sizeof(dns), "127.0.0.1:%u", names_port);
    wifi_port_enable(dns, CONSOLE_MAC, NULL, 0);
    CHECK(wifi_init(nvram) == 1 && wifi_on(), "Wi-Fi Connection does not start");
    check_settings(nvram);

    /* the pages of a run before come back */
    uint8_t kept[WIFI_PORT_SETTINGS_SIZE];
    memcpy(kept, wifi_port_settings(), sizeof(kept));
    kept[0xf0] = 0x12, kept[0xf1] = 0x34;
    kept[0xfe] = (uint8_t) crc16(kept, 0xfe), kept[0xff] = (uint8_t) (crc16(kept, 0xfe) >> 8);
    wifi_port_enable(dns, CONSOLE_MAC, kept, sizeof(kept));
    CHECK(wifi_init(nvram) == 1 && memcmp(wifi_port_settings(), kept, sizeof(kept)) == 0, "kept settings are not put back");

    check_scan();
    wifi_connect(now_cycles);
    check_arp_dhcp();
    check_udp(names_port, echo_port);
    check_tcp(stream_port, closed_port);
    wifi_disconnect();

    printf(failures ? "wifi-test: %d failures\n" : "wifi-test: passed\n", failures);
    fflush(stdout);
    _exit(failures ? 1 : 0);
}
