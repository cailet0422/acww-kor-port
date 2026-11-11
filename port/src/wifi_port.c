/* Nintendo Wi-Fi Connection for the port: the access point the game finds, and behind it the
 * real network (docs/multiplayer.md, "Wi-Fi Connection").
 *
 * This file takes the place of decomp/runtime/wifi.c in the front end's build, with the same
 * interface (decomp/runtime/wifi.h: the wireless manager's model, wm.c, calls it). The
 * decomp's file answers the game's network code from inside the process: an access point, a
 * router, name and account servers with fixed answers, nothing real. Here the access point
 * is the same idea and everything behind it is the network this machine is on.
 *
 * The game has its own TCP/IP: it sends whole IPv4 packets (and ARP) in 802.11 data frames.
 * A phone's app cannot put such packets on the air, so this is a small gateway, the kind an
 * emulator's "user-mode network" is:
 *
 *   - ARP: every address is at the access point;
 *   - DHCP: the console is given 192.168.11.2, the router 192.168.11.1, and the router as
 *     its name server;
 *   - DNS: a query to the router goes, as it is, to the name server the player chose (the
 *     way a private server is reached on a DS: by its DNS), and the answer comes back. With
 *     none chosen, names are looked up by this machine's own resolver;
 *   - UDP: a socket of this machine's for each port the console sends from; what comes back
 *     to it, from anyone, goes to the console (what the game's matches need);
 *   - TCP: the console's connection ends here and one of this machine's goes on to the
 *     address it asked for; the bytes are passed both ways, with the sequence numbers,
 *     acknowledgements and windows the console's stack expects;
 *   - ICMP: the router answers pings.
 *
 * HTTPS is the game's own: its SSL layer runs as the ROM's code and talks to the server
 * directly (the decomp replaces that layer by plain TCP for its simulated servers; not here).
 *
 * The console's settings the game reads and writes for Wi-Fi Connection (the firmware's
 * connection settings, with the user ID the game's library gets from the account server) are
 * made here from nothing, by the published layout (GBATEK, "DS Firmware Wifi Internet Access
 * Points"), and kept between runs by the front end (wifi_port.h). No firmware image is used.
 */
#include "wifi.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "io.h"

#include "wifi_port.h"

#define AP_SSID "acww-kor-port" /* not 8 bytes long (FreeSpot) nor 32 (a Nintendo spot) */
#define AP_CHANNEL 1
#define CYCLES_PER_MS 33514u

#define NET_ROUTER 0xc0a80b01u /* 192.168.11.1 */
#define NET_CLIENT 0xc0a80b02u /* 192.168.11.2 */
#define NET_MASK 0xffffff00u

#define ETHER_ARP 0x0806
#define ETHER_IP 0x0800
#define PROTO_ICMP 1
#define PROTO_TCP 6
#define PROTO_UDP 17

#define FRAMES 256
#define TCP_CONNECTIONS 16
#define TCP_PENDING 16384 /* bytes from a server waiting for the console to take them */
#define SEGMENT 1024     /* the most bytes in a segment to the console */
#define TCP_WINDOW 8192   /* what the console may send ahead: passed on at once */
#define UDP_PORTS 32

#define F_FIN 0x01
#define F_SYN 0x02
#define F_RST 0x04
#define F_PSH 0x08
#define F_ACK 0x10

static const uint8_t AP_MAC[6] = {0x02, 0x00, 0x41, 0x43, 0x57, 0x02};
static const uint8_t LLC[6] = {0xaa, 0xaa, 0x03, 0x00, 0x00, 0x00};

typedef struct Frame {
    uint16_t length;
    uint8_t data[WIFI_FRAME_MAX];
} Frame;

enum { TCP_FREE, TCP_CONNECTING, TCP_OPEN };

typedef struct Tcp {
    int state, fd;
    uint32_t remote_ip;
    uint16_t remote_port, client_port;
    uint32_t rcv_nxt;  /* the next byte expected of the console */
    uint32_t out_seq;  /* the sequence number of pending[0] */
    uint32_t window;   /* the console's */
    uint8_t pending[TCP_PENDING];
    uint32_t pending_length, sent; /* sent: the bytes of pending that went out */
    int host_eof, fin_sent, fin_acked, fin_from_console;
    uint64_t progress_ms; /* when the console last took something */
} Tcp;

typedef struct Udp {
    int used, fd;
    uint16_t client_port;
    uint32_t asked_ip; /* a query to the router's name server: whom the console asked */
    uint64_t used_ms;
} Udp;

static struct {
    int on, trace, trace_packets, associated;
    uint8_t *nvram;
    uint8_t client_mac[6];
    uint8_t mac[6];
    int has_mac;
    struct sockaddr_in dns;
    int has_dns;
    char dns_text[64];
    uint8_t saved[WIFI_PORT_SETTINGS_SIZE];
    int has_saved;
    Frame *frames;
    int head, count;
    uint16_t ip_id;
    uint64_t polled_at;
    Tcp tcp[TCP_CONNECTIONS];
    Udp udp[UDP_PORTS];
    unsigned long sent, received, dropped, connections, lookups;
} wifi;

#define TRACE(...)                                                 \
    do {                                                           \
        if (wifi.trace) {                                          \
            fprintf(stderr, "wifi: frame %u: ", io_frame());       \
            fprintf(stderr, __VA_ARGS__);                          \
            fprintf(stderr, "\n");                                 \
        }                                                          \
    } while (0)

/* Small things ------------------------------------------------------------------------------ */

static void be16(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t) (v >> 8);
    p[1] = (uint8_t) v;
}

static void be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t) (v >> 24);
    p[1] = (uint8_t) (v >> 16);
    p[2] = (uint8_t) (v >> 8);
    p[3] = (uint8_t) v;
}

static uint32_t get_be16(const uint8_t *p)
{
    return (uint32_t) p[0] << 8 | p[1];
}

static uint32_t get_be32(const uint8_t *p)
{
    return (uint32_t) p[0] << 24 | (uint32_t) p[1] << 16 | (uint32_t) p[2] << 8 | p[3];
}

static void le16(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t) v;
    p[1] = (uint8_t) (v >> 8);
}

static uint64_t now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t) t.tv_sec * 1000u + (uint64_t) t.tv_nsec / 1000000u;
}

static void nonblocking(int fd)
{
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
}

/* What the front end sets before the run ---------------------------------------------------- */

void wifi_port_enable(const char *dns, const uint8_t mac[6], const uint8_t *saved, size_t saved_size)
{
    wifi.on = 1;
    wifi.has_dns = 0;
    wifi.dns_text[0] = 0;
    if (dns != NULL && *dns) {
        /* "a.b.c.d" or "a.b.c.d:port" */
        char host[64];
        int port = 53;
        snprintf(host, sizeof(host), "%s", dns);
        char *colon = strchr(host, ':');
        if (colon != NULL) {
            *colon = 0;
            port = atoi(colon + 1);
        }
        memset(&wifi.dns, 0, sizeof(wifi.dns));
        wifi.dns.sin_family = AF_INET;
        wifi.dns.sin_port = htons((uint16_t) port);
        if (inet_pton(AF_INET, host, &wifi.dns.sin_addr) == 1) {
            wifi.has_dns = 1;
            snprintf(wifi.dns_text, sizeof(wifi.dns_text), "%s", dns);
        }
    }
    wifi.has_mac = mac != NULL;
    if (mac != NULL) {
        memcpy(wifi.mac, mac, 6);
    }
    wifi.has_saved = saved != NULL && saved_size == WIFI_PORT_SETTINGS_SIZE;
    if (wifi.has_saved) {
        memcpy(wifi.saved, saved, WIFI_PORT_SETTINGS_SIZE);
    }
}

int wifi_port_mac(uint8_t mac[6])
{
    if (!wifi.on || !wifi.has_mac) {
        return 0;
    }
    memcpy(mac, wifi.mac, 6);
    return 1;
}

const uint8_t *wifi_port_settings(void)
{
    return wifi.on && wifi.nvram != NULL ? wifi.nvram + WIFI_PORT_SETTINGS_AT : NULL;
}

void wifi_port_status(char *text, size_t size)
{
    if (!wifi.on) {
        snprintf(text, size, "off");
        return;
    }
    snprintf(text, size, "%s, name server %s; %lu frames out, %lu in, %lu connections, %lu lookups",
             wifi.associated ? "connected to the access point" : "not connected", wifi.has_dns ? wifi.dns_text : "this machine's",
             wifi.sent, wifi.received, wifi.connections, wifi.lookups);
}

/* The console's connection settings ---------------------------------------------------------- */

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

/* The three connection settings, 0x100 bytes each, below the user settings: the first the
 * access point's (its SSID at 0x40, every address 0: asked of the access point; in use, 0xe7
 * zero; configured, 0xef bit 0), the others not set (0xe7 is 0xff). Each ends in its CRC-16
 * over the 0xfe bytes before. The user ID (0xf0) starts as 0: the game's library gets one
 * from the account server and writes it there, which is why these pages are kept. */
static void settings_make(uint8_t *nvram)
{
    for (int page = 0; page < 4; page++) {
        uint8_t *p = nvram + WIFI_PORT_SETTINGS_AT + 0x100 * page;
        memset(p, 0, 0x100);
        if (page == 0) {
            memcpy(p + 0x40, AP_SSID, strlen(AP_SSID));
            p[0xef] = 1;
        } else {
            p[0xe7] = 0xff;
        }
        uint32_t crc = crc16(p, 0xfe);
        p[0xfe] = (uint8_t) crc;
        p[0xff] = (uint8_t) (crc >> 8);
    }
}

int wifi_init(uint8_t *nvram)
{
    if (!wifi.on) {
        return 0;
    }
    static Frame frames[FRAMES];
    const char *trace = getenv("RUN_TRACE");
    wifi.trace = trace != NULL && strstr(trace, "wifi") != NULL;
    wifi.trace_packets = trace != NULL && strstr(trace, "wifi-packets") != NULL;
    wifi.frames = frames;
    wifi.nvram = nvram;
    for (int i = 0; i < TCP_CONNECTIONS; i++) {
        wifi.tcp[i].fd = -1;
    }
    for (int i = 0; i < UDP_PORTS; i++) {
        wifi.udp[i].fd = -1;
    }
    uint32_t user = ((uint32_t) nvram[0x20] | (uint32_t) nvram[0x21] << 8) * 8;
    if (user != WIFI_PORT_SETTINGS_AT + 0x400) {
        fprintf(stderr, "wifi: the firmware image is not laid out as expected; off\n");
        wifi.on = 0;
        return 0;
    }
    settings_make(nvram);
    if (wifi.has_saved) {
        /* the connection settings of the runs before (the user ID among them) */
        memcpy(nvram + WIFI_PORT_SETTINGS_AT, wifi.saved, 0x400);
    }
    return 1;
}

int wifi_on(void)
{
    return wifi.on;
}

/* The access point ---------------------------------------------------------------------------- */

int wifi_is_ap(const uint8_t mac[6])
{
    return wifi.on && memcmp(mac, AP_MAC, 6) == 0;
}

/* The access point's WMBssDesc, as the wireless manager gives a scan's find: its length in
 * halfwords at 0, the signal at 2, the BSSID at 4, the SSID's length at 0xa and the SSID at
 * 0xc, the capabilities at 0x2c (an access point, open), the rates at 0x2e and 0x30 (1 and
 * 2 Mbps), the beacon period at 0x32, the DTIM period at 0x34, the channel at 0x36. */
int wifi_scan(uint32_t channels, const uint8_t bssid[6], const uint8_t *ssid, uint32_t ssid_length, uint32_t match,
              uint8_t desc[WIFI_BSS_SIZE])
{
    static const uint8_t any[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    uint32_t length = (uint32_t) strlen(AP_SSID);
    if (!wifi.on || !(channels & (1u << (AP_CHANNEL - 1)))) {
        return 0;
    }
    if (memcmp(bssid, any, 6) != 0 && memcmp(bssid, AP_MAC, 6) != 0) {
        return 0;
    }
    if (ssid_length != 0 &&
        (match > length || memcmp(ssid, AP_SSID, match) != 0 || (match == ssid_length && ssid_length != length))) {
        return 0;
    }
    memset(desc, 0, WIFI_BSS_SIZE);
    le16(desc, WIFI_BSS_SIZE / 2);
    le16(desc + 2, 0xd0);
    memcpy(desc + 4, AP_MAC, 6);
    le16(desc + 0xa, length);
    memcpy(desc + 0xc, AP_SSID, length);
    le16(desc + 0x2c, 0x0001);
    le16(desc + 0x2e, 0x0003);
    le16(desc + 0x30, 0x0003);
    le16(desc + 0x32, 100);
    le16(desc + 0x34, 1);
    le16(desc + 0x36, AP_CHANNEL);
    return 1;
}

static void tcp_free(Tcp *c)
{
    if (c->fd >= 0) {
        close(c->fd);
    }
    memset(c, 0, sizeof(*c));
    c->fd = -1;
}

static void sockets_close(void)
{
    for (int i = 0; i < TCP_CONNECTIONS; i++) {
        tcp_free(&wifi.tcp[i]);
    }
    for (int i = 0; i < UDP_PORTS; i++) {
        if (wifi.udp[i].fd >= 0) {
            close(wifi.udp[i].fd);
        }
        memset(&wifi.udp[i], 0, sizeof(wifi.udp[i]));
        wifi.udp[i].fd = -1;
    }
}

void wifi_connect(uint64_t now)
{
    (void) now;
    wifi.associated = 1;
    wifi.head = wifi.count = 0;
    sockets_close();
    TRACE("associated with the access point");
}

void wifi_disconnect(void)
{
    wifi.associated = 0;
    wifi.head = wifi.count = 0;
    sockets_close();
    TRACE("disassociated");
}

/* Packets to the console ---------------------------------------------------------------------- */

/* RUN_TRACE's "wifi-packets": a line for each packet, either way. */
static void packet_trace(const char *way, uint32_t ethertype, const uint8_t *p, uint32_t length)
{
    if (!wifi.trace_packets) {
        return;
    }
    if (ethertype == ETHER_IP && length >= 20) {
        uint32_t header = (uint32_t) (p[0] & 15) * 4, proto = p[9];
        const uint8_t *t = p + header;
        if ((proto == PROTO_UDP || proto == PROTO_TCP) && length >= header + 8) {
            TRACE("%s %s %u.%u.%u.%u:%u > %u.%u.%u.%u:%u, %u bytes%s%s%s%s", way, proto == PROTO_UDP ? "UDP" : "TCP", p[12],
                  p[13], p[14], p[15], get_be16(t), p[16], p[17], p[18], p[19], get_be16(t + 2), length - header,
                  proto == PROTO_TCP && (t[13] & 2) ? " SYN" : "", proto == PROTO_TCP && (t[13] & 16) ? " ACK" : "",
                  proto == PROTO_TCP && (t[13] & 1) ? " FIN" : "", proto == PROTO_TCP && (t[13] & 4) ? " RST" : "");
        } else {
            TRACE("%s IP protocol %u, %u bytes", way, proto, length);
        }
    } else {
        TRACE("%s ethertype %04x, %u bytes", way, ethertype, length);
    }
}

static int frames_room(void)
{
    return FRAMES - wifi.count;
}

static void frame_out(uint32_t ethertype, const uint8_t *payload, uint32_t length)
{
    if (wifi.count == FRAMES || 8 + length > WIFI_FRAME_MAX) {
        wifi.dropped++;
        return;
    }
    Frame *f = &wifi.frames[(wifi.head + wifi.count) % FRAMES];
    memcpy(f->data, LLC, 6);
    be16(f->data + 6, ethertype);
    memcpy(f->data + 8, payload, length);
    f->length = (uint16_t) (8 + length);
    wifi.count++;
    packet_trace("in ", ethertype, payload, length);
}

static uint32_t sum_add(uint32_t sum, const uint8_t *p, uint32_t n)
{
    for (uint32_t i = 0; i + 1 < n; i += 2) {
        sum += get_be16(p + i);
    }
    if (n & 1) {
        sum += (uint32_t) p[n - 1] << 8;
    }
    return sum;
}

static uint32_t sum_end(uint32_t sum)
{
    while (sum >> 16) {
        sum = (sum & 0xffff) + (sum >> 16);
    }
    return ~sum & 0xffff;
}

static uint32_t pseudo_sum(uint32_t src, uint32_t dst, uint32_t proto, uint32_t length)
{
    uint8_t h[12];
    be32(h, src);
    be32(h + 4, dst);
    h[8] = 0;
    h[9] = (uint8_t) proto;
    be16(h + 10, length);
    return sum_add(0, h, 12);
}

static void ip_out(uint32_t proto, uint32_t src, uint32_t dst, const uint8_t *payload, uint32_t length)
{
    uint8_t packet[WIFI_FRAME_MAX];
    if (20 + length > sizeof(packet) - 8) {
        wifi.dropped++;
        return;
    }
    memset(packet, 0, 20);
    packet[0] = 0x45;
    be16(packet + 2, 20 + length);
    be16(packet + 4, wifi.ip_id++);
    packet[8] = 64;
    packet[9] = (uint8_t) proto;
    be32(packet + 12, src);
    be32(packet + 16, dst);
    be16(packet + 10, sum_end(sum_add(0, packet, 20)));
    memcpy(packet + 20, payload, length);
    frame_out(ETHER_IP, packet, 20 + length);
}

static void udp_out(uint32_t src, uint32_t sport, uint32_t dst, uint32_t dport, const uint8_t *data, uint32_t length)
{
    uint8_t d[WIFI_FRAME_MAX];
    if (8 + length > sizeof(d) - 28) {
        wifi.dropped++;
        return;
    }
    be16(d, sport);
    be16(d + 2, dport);
    be16(d + 4, 8 + length);
    be16(d + 6, 0);
    memcpy(d + 8, data, length);
    uint32_t sum = sum_end(sum_add(pseudo_sum(src, dst, PROTO_UDP, 8 + length), d, 8 + length));
    be16(d + 6, sum ? sum : 0xffff);
    ip_out(PROTO_UDP, src, dst, d, 8 + length);
}

/* ARP, DHCP, ping --------------------------------------------------------------------------- */

/* Who has an address? The access point, for every address but the console's own. */
static void arp_in(const uint8_t *p, uint32_t length)
{
    if (length < 28 || get_be16(p + 6) != 1 || get_be32(p + 24) == get_be32(p + 14)) {
        return;
    }
    uint8_t r[28];
    memcpy(r, p, 6);
    be16(r + 6, 2);
    memcpy(r + 8, AP_MAC, 6);
    memcpy(r + 14, p + 24, 4);
    memcpy(r + 18, p + 8, 10);
    frame_out(ETHER_ARP, r, sizeof(r));
}

/* DISCOVER is answered with an OFFER and REQUEST with an ACK: the console's address, the
 * mask, the router, and the router as the name server (the queries are passed on from there). */
static void dhcp_in(const uint8_t *p, uint32_t length)
{
    if (length < 240 || p[0] != 1 || get_be32(p + 236) != 0x63825363u) {
        return;
    }
    int type = 0;
    for (uint32_t i = 240; i + 1 < length && p[i] != 255;) {
        if (p[i] == 0) {
            i++;
            continue;
        }
        if (p[i] == 53 && p[i + 1] >= 1) {
            type = p[i + 2];
        }
        i += 2u + p[i + 1];
    }
    if (type != 1 && type != 3) {
        return;
    }
    uint8_t r[300];
    memset(r, 0, sizeof(r));
    r[0] = 2;
    r[1] = 1;
    r[2] = 6;
    memcpy(r + 4, p + 4, 4);
    be32(r + 16, NET_CLIENT);
    be32(r + 20, NET_ROUTER);
    memcpy(r + 28, p + 28, 16);
    be32(r + 236, 0x63825363u);
    uint8_t *o = r + 240;
    *o++ = 53, *o++ = 1, *o++ = (uint8_t) (type == 1 ? 2 : 5);
    *o++ = 54, *o++ = 4, be32(o, NET_ROUTER), o += 4;
    *o++ = 51, *o++ = 4, be32(o, 86400), o += 4;
    *o++ = 1, *o++ = 4, be32(o, NET_MASK), o += 4;
    *o++ = 3, *o++ = 4, be32(o, NET_ROUTER), o += 4;
    *o++ = 6, *o++ = 4, be32(o, NET_ROUTER), o += 4;
    *o++ = 255;
    udp_out(NET_ROUTER, 67, 0xffffffffu, 68, r, sizeof(r));
    TRACE("DHCP %s", type == 1 ? "offer" : "ack");
}

/* Names ---------------------------------------------------------------------------------------- */

/* Without a name server of the player's choosing: a query for one name's address, answered
 * from this machine's own resolver (which may take a moment). */
static void dns_local(uint32_t asked_ip, uint32_t client_port, const uint8_t *q, uint32_t length)
{
    char name[256];
    uint32_t at = 12, n = 0;
    if (length < 17 || get_be16(q + 4) != 1) {
        return;
    }
    while (at < length && q[at] != 0 && n + q[at] + 2 < sizeof(name)) {
        uint32_t label = q[at++];
        if (at + label > length) {
            return;
        }
        memcpy(name + n, q + at, label);
        n += label;
        name[n++] = '.';
        at += label;
    }
    name[n ? n - 1 : 0] = 0;
    if (at + 5 > length) {
        return;
    }
    uint32_t question_end = at + 5, type = get_be16(q + at + 1);
    uint8_t r[512];
    if (question_end + 16 > sizeof(r)) {
        return;
    }
    memcpy(r, q, question_end);
    be16(r + 2, 0x8180);
    be16(r + 6, 0);
    be16(r + 8, 0);
    be16(r + 10, 0);
    uint32_t out = question_end;
    struct addrinfo hints, *found = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (type == 1 && getaddrinfo(name, NULL, &hints, &found) == 0 && found != NULL) {
        be16(r + 6, 1);
        be16(r + out, 0xc00c);
        be16(r + out + 2, 1);
        be16(r + out + 4, 1);
        be32(r + out + 6, 60);
        be16(r + out + 10, 4);
        memcpy(r + out + 12, &((struct sockaddr_in *) found->ai_addr)->sin_addr, 4);
        out += 16;
    } else {
        be16(r + 2, 0x8183); /* no such name */
    }
    if (found != NULL) {
        freeaddrinfo(found);
    }
    TRACE("name %s: %s", name, get_be16(r + 6) ? "found" : "not found");
    udp_out(asked_ip, 53, NET_CLIENT, client_port, r, out);
}

/* UDP ------------------------------------------------------------------------------------------ */

static Udp *udp_for(uint32_t client_port)
{
    Udp *u = NULL, *oldest = &wifi.udp[0];
    for (int i = 0; i < UDP_PORTS; i++) {
        Udp *c = &wifi.udp[i];
        if (c->used && c->client_port == client_port) {
            return c;
        }
        if (!c->used && u == NULL) {
            u = c;
        }
        if (c->used_ms < oldest->used_ms) {
            oldest = c;
        }
    }
    if (u == NULL) {
        u = oldest; /* the port not used for the longest */
        close(u->fd);
    }
    memset(u, 0, sizeof(*u));
    u->fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (u->fd < 0) {
        return NULL;
    }
    nonblocking(u->fd);
    u->used = 1;
    u->client_port = (uint16_t) client_port;
    return u;
}

static void udp_in(uint32_t dst, const uint8_t *d, uint32_t length)
{
    if (length < 8) {
        return;
    }
    uint32_t sport = get_be16(d), dport = get_be16(d + 2);
    const uint8_t *data = d + 8;
    uint32_t n = length - 8;
    if (dport == 67) {
        dhcp_in(data, n);
        return;
    }
    if (dst == 0xffffffffu || (dst & ~NET_MASK) == ~NET_MASK) {
        return; /* a broadcast: nobody else is on this network */
    }
    struct sockaddr_in to;
    memset(&to, 0, sizeof(to));
    to.sin_family = AF_INET;
    to.sin_port = htons((uint16_t) dport);
    to.sin_addr.s_addr = htonl(dst);
    uint32_t asked = 0;
    if (dport == 53 && dst == NET_ROUTER) {
        wifi.lookups++;
        if (!wifi.has_dns) {
            dns_local(dst, sport, data, n);
            return;
        }
        to = wifi.dns;
        asked = dst;
    }
    Udp *u = udp_for(sport);
    if (u == NULL) {
        return;
    }
    u->asked_ip = asked ? asked : u->asked_ip;
    u->used_ms = now_ms();
    sendto(u->fd, data, n, MSG_NOSIGNAL, (struct sockaddr *) &to, sizeof(to));
}

static void udp_poll(void)
{
    for (int i = 0; i < UDP_PORTS; i++) {
        Udp *u = &wifi.udp[i];
        while (u->used && frames_room() > 8) {
            uint8_t data[WIFI_FRAME_MAX];
            struct sockaddr_in from;
            socklen_t size = sizeof(from);
            ssize_t n = recvfrom(u->fd, data, sizeof(data) - 64, 0, (struct sockaddr *) &from, &size);
            if (n < 0) {
                break;
            }
            uint32_t ip = ntohl(from.sin_addr.s_addr), port = ntohs(from.sin_port);
            if (wifi.has_dns && u->asked_ip != 0 && from.sin_addr.s_addr == wifi.dns.sin_addr.s_addr && from.sin_port == wifi.dns.sin_port) {
                ip = u->asked_ip; /* the name server's answer, as from whom the console asked */
                port = 53;
            }
            u->used_ms = now_ms();
            udp_out(ip, port, NET_CLIENT, u->client_port, data, (uint32_t) n);
        }
    }
}

/* TCP ------------------------------------------------------------------------------------------ */

static void tcp_out(const Tcp *c, uint32_t seq, uint32_t flags, const uint8_t *data, uint32_t length)
{
    uint8_t s[24 + SEGMENT];
    uint32_t header = (flags & F_SYN) ? 24 : 20;
    memset(s, 0, header);
    be16(s, c->remote_port);
    be16(s + 2, c->client_port);
    be32(s + 4, seq);
    be32(s + 8, c->rcv_nxt);
    s[12] = (uint8_t) (header / 4 << 4);
    s[13] = (uint8_t) flags;
    be16(s + 14, TCP_WINDOW);
    if (flags & F_SYN) {
        s[20] = 2;
        s[21] = 4;
        be16(s + 22, SEGMENT);
    }
    if (length != 0) {
        memcpy(s + header, data, length);
    }
    be16(s + 16, sum_end(sum_add(pseudo_sum(c->remote_ip, NET_CLIENT, PROTO_TCP, header + length), s, header + length)));
    ip_out(PROTO_TCP, c->remote_ip, NET_CLIENT, s, header + length);
}

/* A segment that belongs to no connection: reset. */
static void tcp_refuse(uint32_t remote_ip, const uint8_t *seg, uint32_t length)
{
    Tcp c;
    memset(&c, 0, sizeof(c));
    uint32_t offset = (uint32_t) (seg[12] >> 4) * 4, flags = seg[13];
    if (flags & F_RST) {
        return;
    }
    c.remote_ip = remote_ip;
    c.client_port = (uint16_t) get_be16(seg);
    c.remote_port = (uint16_t) get_be16(seg + 2);
    c.rcv_nxt = get_be32(seg + 4) + (length - offset) + ((flags & F_SYN) ? 1 : 0) + ((flags & F_FIN) ? 1 : 0);
    tcp_out(&c, (flags & F_ACK) ? get_be32(seg + 8) : 0, F_RST | F_ACK, NULL, 0);
}

/* What waits for the console goes out, as far as its window lets it; then the end, once the
 * server has said its last. */
static void tcp_push(Tcp *c)
{
    while (c->sent < c->pending_length && c->sent < c->window && frames_room() > 4) {
        uint32_t n = c->pending_length - c->sent;
        n = n > SEGMENT ? SEGMENT : n;
        n = n > c->window - c->sent ? c->window - c->sent : n;
        tcp_out(c, c->out_seq + c->sent, F_ACK | F_PSH, c->pending + c->sent, n);
        c->sent += n;
    }
    if (c->host_eof && c->pending_length == 0 && !c->fin_sent && frames_room() > 2) {
        tcp_out(c, c->out_seq, F_ACK | F_FIN, NULL, 0);
        c->fin_sent = 1;
    }
}

static void tcp_in(uint32_t remote_ip, const uint8_t *seg, uint32_t length)
{
    if (length < 20) {
        return;
    }
    uint32_t client_port = get_be16(seg), remote_port = get_be16(seg + 2), seq = get_be32(seg + 4), ack = get_be32(seg + 8);
    uint32_t offset = (uint32_t) (seg[12] >> 4) * 4, flags = seg[13];
    if (offset < 20 || offset > length) {
        return;
    }
    const uint8_t *data = seg + offset;
    uint32_t n = length - offset;
    Tcp *c = NULL, *spare = NULL;
    for (int i = 0; i < TCP_CONNECTIONS; i++) {
        Tcp *t = &wifi.tcp[i];
        if (t->state != TCP_FREE && t->remote_ip == remote_ip && t->remote_port == remote_port && t->client_port == client_port) {
            c = t;
        } else if (t->state == TCP_FREE && spare == NULL) {
            spare = t;
        }
    }
    if (flags & F_RST) {
        if (c != NULL) {
            TRACE("TCP %u: reset by the console", client_port);
            tcp_free(c);
        }
        return;
    }
    if ((flags & F_SYN) && !(flags & F_ACK)) {
        if (c != NULL && c->state == TCP_OPEN && seq + 1 == c->rcv_nxt && c->pending_length == 0) {
            tcp_out(c, c->out_seq - 1, F_SYN | F_ACK, NULL, 0); /* its SYN again: the answer again */
            return;
        }
        if (c != NULL) {
            return; /* still on its way out */
        }
        if (spare == NULL) {
            tcp_refuse(remote_ip, seg, length);
            return;
        }
        c = spare;
        memset(c, 0, sizeof(*c));
        c->fd = socket(AF_INET, SOCK_STREAM, 0);
        if (c->fd < 0) {
            tcp_refuse(remote_ip, seg, length);
            c->fd = -1;
            return;
        }
        int one = 1;
        nonblocking(c->fd);
        setsockopt(c->fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
        struct sockaddr_in to;
        memset(&to, 0, sizeof(to));
        to.sin_family = AF_INET;
        to.sin_port = htons((uint16_t) remote_port);
        to.sin_addr.s_addr = htonl(remote_ip);
        if (connect(c->fd, (struct sockaddr *) &to, sizeof(to)) != 0 && errno != EINPROGRESS) {
            tcp_refuse(remote_ip, seg, length);
            close(c->fd);
            c->fd = -1;
            return;
        }
        c->state = TCP_CONNECTING;
        c->remote_ip = remote_ip;
        c->remote_port = (uint16_t) remote_port;
        c->client_port = (uint16_t) client_port;
        c->rcv_nxt = seq + 1;
        c->window = get_be16(seg + 14);
        c->out_seq = ((uint32_t) rand() << 16) ^ (uint32_t) rand() ^ (uint32_t) now_ms();
        c->progress_ms = now_ms();
        wifi.connections++;
        TRACE("TCP %u: to %u.%u.%u.%u:%u", client_port, remote_ip >> 24, (remote_ip >> 16) & 255, (remote_ip >> 8) & 255,
              remote_ip & 255, remote_port);
        return;
    }
    if (c == NULL || c->state != TCP_OPEN) {
        if (c == NULL) {
            tcp_refuse(remote_ip, seg, length);
        }
        return;
    }
    c->window = get_be16(seg + 14);
    if (flags & F_ACK) {
        uint32_t taken = ack - c->out_seq;
        if (taken != 0 && taken <= c->sent) {
            memmove(c->pending, c->pending + taken, c->pending_length - taken);
            c->pending_length -= taken;
            c->sent -= taken;
            c->out_seq = ack;
            c->progress_ms = now_ms();
        } else if (c->fin_sent && c->pending_length == 0 && taken == 1) {
            c->fin_acked = 1;
            c->out_seq = ack;
        }
    }
    int answer = 0;
    if (n != 0) {
        /* bytes sent again that were taken before are passed over: a segment the server's
           side took a part of comes again whole */
        uint32_t before = c->rcv_nxt - seq;
        if (before < n && !c->fin_from_console) {
            ssize_t done = send(c->fd, data + before, n - before, MSG_NOSIGNAL);
            if (done < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                TRACE("TCP %u: the server is gone", client_port);
                tcp_out(c, c->out_seq + c->sent, F_RST | F_ACK, NULL, 0);
                tcp_free(c);
                return;
            }
            /* what the server's side did not take now is not acknowledged: it comes again */
            c->rcv_nxt += done > 0 ? (uint32_t) done : 0;
        }
        answer = 1;
    }
    if ((flags & F_FIN) && seq + n == c->rcv_nxt && !c->fin_from_console) {
        c->rcv_nxt++;
        c->fin_from_console = 1;
        shutdown(c->fd, SHUT_WR);
        answer = 1;
    } else if (flags & F_FIN) {
        answer = 1;
    }
    if (answer) {
        tcp_out(c, c->out_seq + c->sent + (c->fin_sent ? 1 : 0), F_ACK, NULL, 0);
    }
    tcp_push(c);
    if (c->fin_from_console && c->fin_acked) {
        TRACE("TCP %u: closed", client_port);
        tcp_free(c);
    }
}

static void tcp_poll(void)
{
    uint64_t now = now_ms();
    for (int i = 0; i < TCP_CONNECTIONS; i++) {
        Tcp *c = &wifi.tcp[i];
        if (c->state == TCP_CONNECTING) {
            struct pollfd wait = {c->fd, POLLOUT, 0};
            int error = 0;
            socklen_t size = sizeof(error);
            if (poll(&wait, 1, 0) == 1) {
                if (getsockopt(c->fd, SOL_SOCKET, SO_ERROR, &error, &size) == 0 && error == 0) {
                    c->state = TCP_OPEN;
                    tcp_out(c, c->out_seq, F_SYN | F_ACK, NULL, 0);
                    c->out_seq++;
                    TRACE("TCP %u: connected", c->client_port);
                } else {
                    TRACE("TCP %u: refused", c->client_port);
                    c->rcv_nxt = c->rcv_nxt; /* the reset acknowledges the console's SYN */
                    tcp_out(c, 0, F_RST | F_ACK, NULL, 0);
                    tcp_free(c);
                }
            } else if (now - c->progress_ms > 20000) {
                tcp_out(c, 0, F_RST | F_ACK, NULL, 0);
                tcp_free(c);
            }
            continue;
        }
        if (c->state != TCP_OPEN) {
            continue;
        }
        if (!c->host_eof && c->pending_length < TCP_PENDING) {
            ssize_t n = recv(c->fd, c->pending + c->pending_length, TCP_PENDING - c->pending_length, 0);
            if (n > 0) {
                c->pending_length += (uint32_t) n;
            } else if (n == 0) {
                c->host_eof = 1;
            } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
                TRACE("TCP %u: the server reset", c->client_port);
                tcp_out(c, c->out_seq + c->sent, F_RST | F_ACK, NULL, 0);
                tcp_free(c);
                continue;
            }
        }
        /* nothing taken for three seconds with something sent: once more from the start */
        if (c->sent != 0 && now - c->progress_ms > 3000) {
            c->sent = 0;
            c->progress_ms = now;
        }
        tcp_push(c);
        if ((c->fin_from_console && c->fin_acked) || (c->fin_sent && c->fin_from_console && now - c->progress_ms > 10000)) {
            tcp_free(c);
        }
    }
}

/* The console's frames ------------------------------------------------------------------------ */

static void ip_in(const uint8_t *p, uint32_t length)
{
    if (length < 20 || (p[0] >> 4) != 4) {
        return;
    }
    uint32_t header = (uint32_t) (p[0] & 15) * 4, total = get_be16(p + 2), proto = p[9], dst = get_be32(p + 16);
    if (header < 20 || total > length || total < header || (get_be16(p + 6) & 0x3fff) != 0) {
        return; /* (fragments are not put together: the game sends none) */
    }
    const uint8_t *payload = p + header;
    uint32_t n = total - header;
    if (proto == PROTO_UDP) {
        udp_in(dst, payload, n);
    } else if (proto == PROTO_TCP) {
        tcp_in(dst, payload, n);
    } else if (proto == PROTO_ICMP && n >= 8 && payload[0] == 8 && dst == NET_ROUTER) {
        uint8_t r[WIFI_FRAME_MAX];
        if (n <= sizeof(r) - 28) {
            memcpy(r, payload, n);
            r[0] = 0;
            be16(r + 2, 0);
            be16(r + 2, sum_end(sum_add(0, r, n)));
            ip_out(PROTO_ICMP, NET_ROUTER, get_be32(p + 12), r, n);
        }
    }
}

void wifi_send(uint64_t now, const uint8_t src[6], const uint8_t dst[6], const uint8_t *frame, uint32_t length)
{
    (void) now;
    (void) dst;
    if (!wifi.associated || length < 8 || memcmp(frame, LLC, 6) != 0) {
        return;
    }
    memcpy(wifi.client_mac, src, 6);
    wifi.sent++;
    uint32_t type = get_be16(frame + 6);
    packet_trace("out", type, frame + 8, length - 8);
    if (type == ETHER_ARP) {
        arp_in(frame + 8, length - 8);
    } else if (type == ETHER_IP) {
        ip_in(frame + 8, length - 8);
    }
}

uint32_t wifi_receive(uint64_t now, uint8_t src[6], uint8_t dst[6], uint8_t *frame, uint32_t room)
{
    if (!wifi.associated) {
        return 0;
    }
    /* this machine's sockets are looked at once in a millisecond of the DS's time */
    if (now - wifi.polled_at >= CYCLES_PER_MS || now < wifi.polled_at) {
        wifi.polled_at = now;
        udp_poll();
        tcp_poll();
    }
    if (wifi.count == 0) {
        return 0;
    }
    Frame *f = &wifi.frames[wifi.head];
    wifi.head = (wifi.head + 1) % FRAMES;
    wifi.count--;
    if (f->length > room) {
        wifi.dropped++;
        return 0;
    }
    memcpy(src, AP_MAC, 6);
    memcpy(dst, wifi.client_mac, 6);
    memcpy(frame, f->data, f->length);
    wifi.received++;
    return f->length;
}

/* The game's SSL layer is left as it is. */
ArmHook wifi_hook_at(uint32_t address, void **hook_user)
{
    (void) address;
    (void) hook_user;
    return NULL;
}

void wifi_report(FILE *out)
{
    char text[256];
    if (wifi.on) {
        wifi_port_status(text, sizeof(text));
        fprintf(out, "wifi: %s\n", text);
    }
}
