/* The wireless of this console carried to the other consoles of a room over the network
 * (netlink.h, link_proto.h, docs/multiplayer.md).
 *
 * What the model of the wireless manager does with another console (decomp/runtime/wm.c,
 * docs/multi-console.md): once a frame it writes one message, everything its radio sent in
 * the frame, and reads one, everything the other's sent in the same frame. On one machine
 * the two wait for each other at a pipe, frame by frame. Over a network that would hold
 * every frame for the journey there and back, so here it is loosened:
 *
 *   - each frame's message to a console goes out at once, numbered, with the number of that
 *     console's messages this one has taken so far;
 *   - what a console sends is taken in the order it came, one message a frame. When none has
 *     arrived a moment after it was due, the frame goes on with a message of nothing, as
 *     when a radio hears nothing for a moment; the game's own protocol expects that of the
 *     air. The stream is then a frame later for good, which is how the wait for a far
 *     console settles at what the journey needs;
 *   - a console far ahead of what another has taken (its frames come faster) waits for it;
 *     one a long way behind in what it has still to take takes two messages in a frame now
 *     and then;
 *   - a console of the room that is not there, or went away, sends nothing; when it went
 *     away, the model is told as much as concerns it (wm_port_lost_record).
 *
 * The run is no longer the same to the cycle as another run, as the decomp's linked runs
 * are: when a message arrives depends on the network.
 */
#include "netlink.h"

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "io.h"

#include "link_proto.h"
#include "relay.h"

#define HANDLE_BASE 0x40000000 /* a console's handle: this and its number */
#define MESSAGE_MAX 4096       /* wm.c's LINK_MAX: one frame's message */
#define QUEUE 64               /* messages of a console waiting to be taken */
#define QUEUE_HIGH 10          /* more than these waiting: two are taken in a frame */
#define LEAD_MAX 20            /* messages a console may be behind in taking this one's */
#define LATE_MS 6              /* how long a message that is due is waited for */
#define GONE_MS 5000           /* a console that takes nothing for this long is gone */

typedef struct Peer {
    int present;        /* in the room */
    int lost;           /* it went away: the model has not been told yet */
    uint32_t sent;      /* messages sent to it */
    uint32_t taken_by;  /* how many of them it has taken, by its own word */
    uint32_t taken;     /* messages of its that this console took */
    uint16_t length[QUEUE];
    uint8_t message[QUEUE][MESSAGE_MAX];
    int first, count;
    /* the message wm.c is writing, and the one it is reading */
    uint8_t out[8 + MESSAGE_MAX];
    int out_length;
    uint8_t in[8 + 2 * MESSAGE_MAX];
    int in_length, in_at;
    uint32_t frames_since_two;
    unsigned long empties, twos, waits;
} Peer;

static struct {
    int socket;
    int slot;
    int connected;
    int hosting, port;
    char room[LINK_ROOM_NAME + 1], server[128], run_link[96];
    pthread_mutex_t lock, sending;
    pthread_cond_t moved;
    Peer peer[LINK_SLOTS];
    int rtt_ms;
} nl = {.socket = -1, .slot = -1, .lock = PTHREAD_MUTEX_INITIALIZER, .sending = PTHREAD_MUTEX_INITIALIZER,
        .moved = PTHREAD_COND_INITIALIZER, .rtt_ms = -1};

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t) p[0] | (uint32_t) p[1] << 8 | (uint32_t) p[2] << 16 | (uint32_t) p[3] << 24;
}

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t) v;
    p[1] = (uint8_t) (v >> 8);
    p[2] = (uint8_t) (v >> 16);
    p[3] = (uint8_t) (v >> 24);
}

static uint64_t now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t) t.tv_sec * 1000u + (uint64_t) t.tv_nsec / 1000000u;
}

/* The socket ------------------------------------------------------------------------------- */

static int send_all(const uint8_t *p, size_t n)
{
    while (n > 0) {
        ssize_t done = send(nl.socket, p, n, MSG_NOSIGNAL);
        if (done < 0 && errno == EINTR) {
            continue;
        }
        if (done <= 0) {
            return -1;
        }
        p += done;
        n -= (size_t) done;
    }
    return 0;
}

static int send_packet(int kind, int slot, const uint8_t *body, int length)
{
    uint8_t packet[4 + LINK_BODY_MAX];
    int result = -1;
    if (length > LINK_BODY_MAX) {
        return -1;
    }
    packet[0] = (uint8_t) kind;
    packet[1] = (uint8_t) slot;
    packet[2] = (uint8_t) length;
    packet[3] = (uint8_t) (length >> 8);
    memcpy(packet + 4, body, (size_t) length);
    pthread_mutex_lock(&nl.sending);
    if (nl.socket >= 0) {
        result = send_all(packet, (size_t) length + 4);
    }
    pthread_mutex_unlock(&nl.sending);
    return result;
}

static int receive_all(uint8_t *p, size_t n, int timeout_ms)
{
    while (n > 0) {
        struct pollfd fd = {nl.socket, POLLIN, 0};
        int ready = poll(&fd, 1, timeout_ms);
        if (ready == 0) {
            return 1; /* nothing yet */
        }
        if (ready < 0 && errno == EINTR) {
            continue;
        }
        ssize_t done = ready > 0 ? recv(nl.socket, p, n, 0) : -1;
        if (done <= 0) {
            return -1;
        }
        p += done;
        n -= (size_t) done;
        timeout_ms = 5000; /* the rest of a packet that has begun */
    }
    return 0;
}

/* Under the lock: who is in the room now. A console that came starts from nothing; one that
 * went is lost, which the model is told when it next asks for that console's message. */
static void members(uint8_t mask)
{
    for (int k = 0; k < LINK_SLOTS; k++) {
        Peer *p = &nl.peer[k];
        int there = k != nl.slot && (mask & (1 << k));
        if (there && !p->present) {
            p->sent = p->taken_by = p->taken = 0;
            p->first = p->count = 0;
            p->lost = 0;
            p->frames_since_two = 0;
        } else if (!there && p->present) {
            p->lost = 1;
            p->first = p->count = 0;
        }
        p->present = there;
    }
    pthread_cond_broadcast(&nl.moved);
}

static void *io_thread(void *arg)
{
    uint64_t pinged = 0;
    (void) arg;
    for (;;) {
        uint8_t head[4], body[LINK_BODY_MAX];
        uint64_t now = now_ms();
        if (now - pinged >= 2000) {
            uint8_t stamp[8];
            put32(stamp, (uint32_t) now);
            put32(stamp + 4, (uint32_t) (now >> 32));
            send_packet(LINK_PING, 0, stamp, sizeof(stamp));
            pinged = now;
        }
        int got = receive_all(head, 4, 500);
        if (got == 1) {
            continue;
        }
        int length = head[2] | head[3] << 8;
        if (got != 0 || length > LINK_BODY_MAX || receive_all(body, (size_t) length, 5000) != 0) {
            break;
        }
        pthread_mutex_lock(&nl.lock);
        if (head[0] == LINK_MEMBERS && length >= 1) {
            members(body[0]);
        } else if (head[0] == LINK_PONG && length >= 8) {
            nl.rtt_ms = (int) (now_ms() - ((uint64_t) get32(body) | (uint64_t) get32(body + 4) << 32));
        } else if (head[0] == LINK_DATA && head[1] < LINK_SLOTS && length >= 8 && length - 8 <= MESSAGE_MAX) {
            Peer *p = &nl.peer[head[1]];
            uint32_t taken_by = get32(body + 4);
            if (p->present && taken_by > p->taken_by) {
                p->taken_by = taken_by;
            }
            if (p->present && p->count < QUEUE) {
                int at = (p->first + p->count) % QUEUE;
                p->length[at] = (uint16_t) (length - 8);
                memcpy(p->message[at], body + 8, (size_t) length - 8);
                p->count++;
            } else if (p->present) {
                p->present = 0; /* it sends and sends and nothing is taken: the run has stopped here */
                p->lost = 1;
                p->first = p->count = 0;
            }
            pthread_cond_broadcast(&nl.moved);
        }
        pthread_mutex_unlock(&nl.lock);
    }
    /* the relay is gone: so is everybody */
    pthread_mutex_lock(&nl.lock);
    nl.connected = 0;
    members(0);
    pthread_mutex_unlock(&nl.lock);
    pthread_mutex_lock(&nl.sending);
    close(nl.socket);
    nl.socket = -1;
    pthread_mutex_unlock(&nl.sending);
    return NULL;
}

/* A TCP connection to "host" or "host:port", waited for five seconds at most. */
static int connect_to(const char *server, int default_port)
{
    char host[128], port[16];
    snprintf(host, sizeof(host), "%s", server);
    snprintf(port, sizeof(port), "%d", default_port);
    char *colon = strrchr(host, ':');
    if (colon != NULL && strchr(host, ':') == colon) { /* one colon: a port (not an IPv6 address) */
        *colon = 0;
        snprintf(port, sizeof(port), "%s", colon + 1);
    }
    struct addrinfo hints, *found = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, port, &hints, &found) != 0) {
        return -1;
    }
    int fd = -1;
    for (struct addrinfo *a = found; a != NULL && fd < 0; a = a->ai_next) {
        fd = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (fd < 0) {
            continue;
        }
        fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
        int started = connect(fd, a->ai_addr, a->ai_addrlen), error = 0;
        socklen_t size = sizeof(error);
        struct pollfd wait = {fd, POLLOUT, 0};
        if ((started == 0 || errno == EINPROGRESS) && poll(&wait, 1, 5000) == 1 &&
            getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &size) == 0 && error == 0) {
            int one = 1;
            struct timeval two_seconds = {2, 0};
            fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) & ~O_NONBLOCK);
            setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
            setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &two_seconds, sizeof(two_seconds));
        } else {
            close(fd);
            fd = -1;
        }
    }
    freeaddrinfo(found);
    return fd;
}

int netlink_start(const char *server, const char *room, int port, char *error, size_t error_size)
{
    uint8_t hello[1 + LINK_ROOM_NAME], head[4], body[LINK_BODY_MAX];
    pthread_t thread;
    port = port > 0 ? port : LINK_PORT;
    snprintf(nl.room, sizeof(nl.room), "%s", room != NULL && *room ? room : "acww");
    nl.hosting = server == NULL || *server == 0;
    nl.port = port;
    if (nl.hosting) {
        if (relay_start(port) != 0) {
            snprintf(error, error_size, "cannot open a room on port %d (in use?)", port);
            return -1;
        }
        snprintf(nl.server, sizeof(nl.server), "127.0.0.1:%d", port);
    } else {
        snprintf(nl.server, sizeof(nl.server), "%s", server);
    }
    nl.socket = connect_to(nl.server, LINK_PORT);
    if (nl.socket < 0) {
        snprintf(error, error_size, "no answer from %s", nl.server);
        return -1;
    }
    hello[0] = LINK_VERSION;
    size_t name = strlen(nl.room);
    memcpy(hello + 1, nl.room, name);
    int length = 0;
    if (send_packet(LINK_HELLO, 0, hello, (int) (1 + name)) != 0 || receive_all(head, 4, 5000) != 0 ||
        (length = head[2] | head[3] << 8) > LINK_BODY_MAX || receive_all(body, (size_t) length, 5000) != 0 ||
        head[0] != LINK_WELCOME || head[1] >= LINK_SLOTS || length < 1) {
        snprintf(error, error_size, head[0] == LINK_REFUSED ? "the room \"%s\" is full" : "%s is not a relay of this version",
                 head[0] == LINK_REFUSED ? nl.room : nl.server);
        close(nl.socket);
        nl.socket = -1;
        return -1;
    }
    nl.slot = head[1];
    nl.connected = 1;
    pthread_mutex_lock(&nl.lock);
    members(body[0]);
    pthread_mutex_unlock(&nl.lock);
    /* RUN_LINK: this console, four of them, and a pair of ends (the same handle) for each other */
    int at = snprintf(nl.run_link, sizeof(nl.run_link), "%d,%d", nl.slot, LINK_SLOTS);
    for (int k = 0; k < LINK_SLOTS; k++) {
        if (k != nl.slot) {
            at += snprintf(nl.run_link + at, sizeof(nl.run_link) - (size_t) at, ",%d,%d", HANDLE_BASE + k, HANDLE_BASE + k);
        }
    }
    if (pthread_create(&thread, NULL, io_thread, NULL) != 0) {
        snprintf(error, error_size, "no thread");
        return -1;
    }
    pthread_detach(thread);
    return nl.slot;
}

const char *netlink_run_link(void)
{
    return nl.slot >= 0 ? nl.run_link : NULL;
}

/* This machine's address on its network, for those who are to join a room opened here. */
static void own_address(char *text, size_t size)
{
    struct sockaddr_in to, own;
    socklen_t length = sizeof(own);
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    snprintf(text, size, "?");
    memset(&to, 0, sizeof(to));
    to.sin_family = AF_INET;
    to.sin_port = htons(9);
    to.sin_addr.s_addr = htonl(0xc0000201u); /* 192.0.2.1: nothing is sent, the route is asked for */
    if (fd >= 0 && connect(fd, (struct sockaddr *) &to, sizeof(to)) == 0 && getsockname(fd, (struct sockaddr *) &own, &length) == 0) {
        uint32_t a = ntohl(own.sin_addr.s_addr);
        snprintf(text, size, "%u.%u.%u.%u", a >> 24, (a >> 16) & 255, (a >> 8) & 255, a & 255);
    }
    if (fd >= 0) {
        close(fd);
    }
}

void netlink_status(char *text, size_t size)
{
    if (nl.slot < 0) {
        snprintf(text, size, "off");
        return;
    }
    char who[32] = "", where[96], address[32];
    unsigned long empties = 0, twos = 0, waits = 0;
    pthread_mutex_lock(&nl.lock);
    for (int k = 0; k < LINK_SLOTS; k++) {
        if (nl.peer[k].present) {
            snprintf(who + strlen(who), sizeof(who) - strlen(who), " %d", k);
        }
        empties += nl.peer[k].empties;
        twos += nl.peer[k].twos;
        waits += nl.peer[k].waits;
    }
    int connected = nl.connected, rtt = nl.rtt_ms;
    pthread_mutex_unlock(&nl.lock);
    if (nl.hosting) {
        own_address(address, sizeof(address));
        snprintf(where, sizeof(where), "opened here, %s:%d", address, nl.port);
    } else {
        snprintf(where, sizeof(where), "at %.80s", nl.server);
    }
    snprintf(text, size, "room \"%s\" %s, console %d, %s%s%s; relay %d ms; late %lu, doubled %lu, waited %lu", nl.room, where,
             nl.slot, !connected ? "the relay is gone" : *who ? "with" : "alone", connected ? who : "", "", rtt, empties, twos,
             waits);
}

/* What wm.c writes and reads ----------------------------------------------------------------- */

ssize_t netlink_write(int handle, const void *from, size_t length)
{
    int k = handle - HANDLE_BASE;
    if (k < 0 || k >= LINK_SLOTS) {
        return write(handle, from, length);
    }
    Peer *p = &nl.peer[k];
    if (p->out_length + (int) length > (int) sizeof(p->out)) {
        p->out_length = 0;
        return (ssize_t) length;
    }
    memcpy(p->out + p->out_length, from, length);
    p->out_length += (int) length;
    /* a head of the frame and the length, then that many bytes: the message is whole */
    if (p->out_length >= 8 && p->out_length >= 8 + (int) get32(p->out + 4)) {
        uint8_t body[8 + MESSAGE_MAX];
        int records = p->out_length - 8, send = 0;
        pthread_mutex_lock(&nl.lock);
        if (p->present && nl.connected) {
            put32(body, ++p->sent);
            put32(body + 4, p->taken);
            send = 1;
        }
        pthread_mutex_unlock(&nl.lock);
        if (send) {
            memcpy(body + 8, p->out + 8, (size_t) records);
            send_packet(LINK_DATA, k, body, 8 + records);
        }
        p->out_length = 0;
    }
    return (ssize_t) length;
}

/* Under the lock: waits for something of the peer's to move, `ms` at most. */
static void wait_moved(int ms)
{
    struct timespec until;
    clock_gettime(CLOCK_REALTIME, &until);
    until.tv_nsec += (long) ms * 1000000L;
    until.tv_sec += until.tv_nsec / 1000000000L;
    until.tv_nsec %= 1000000000L;
    pthread_cond_timedwait(&nl.moved, &nl.lock, &until);
}

/* The message of console `k` for this frame, into p->in: the head wm.c expects (this frame's
 * number, the length), then what the console's radio sent, or nothing. */
static void next_message(int k)
{
    Peer *p = &nl.peer[k];
    int length = 0;
    pthread_mutex_lock(&nl.lock);
    if (p->lost) {
        p->lost = 0;
        pthread_mutex_unlock(&nl.lock);
        length = wm_port_lost_record(k, p->in + 8); /* (reads the model's state: not under the lock) */
        pthread_mutex_lock(&nl.lock);
    } else if (p->present) {
        if (p->count == 0) {
            /* none has come. Far ahead of what it has taken: it is waited for. Else a moment,
               for one that is only late */
            uint64_t start = now_ms();
            if (p->sent - p->taken_by > LEAD_MAX) {
                p->waits++;
                while (p->present && p->count == 0 && p->sent - p->taken_by > LEAD_MAX && now_ms() - start < GONE_MS) {
                    wait_moved(50);
                }
                if (p->present && p->count == 0 && p->sent - p->taken_by > LEAD_MAX) {
                    p->present = 0; /* nothing for five seconds: gone, as far as this console can tell */
                    pthread_mutex_unlock(&nl.lock);
                    length = wm_port_lost_record(k, p->in + 8);
                    pthread_mutex_lock(&nl.lock);
                }
            } else {
                while (p->present && p->count == 0 && now_ms() - start < LATE_MS) {
                    wait_moved(2);
                }
            }
        }
        if (p->present && p->count > 0) {
            int two = p->count > QUEUE_HIGH && p->frames_since_two > 30 &&
                      p->length[p->first] + p->length[(p->first + 1) % QUEUE] <= MESSAGE_MAX;
            for (int i = 0; i < (two ? 2 : 1); i++) {
                memcpy(p->in + 8 + length, p->message[p->first], p->length[p->first]);
                length += p->length[p->first];
                p->first = (p->first + 1) % QUEUE;
                p->count--;
                p->taken++;
            }
            p->twos += (unsigned long) two;
            p->frames_since_two = two ? 0 : p->frames_since_two + 1;
        } else if (p->present) {
            p->empties++;
        }
    }
    pthread_mutex_unlock(&nl.lock);
    put32(p->in, io_frame());
    put32(p->in + 4, (uint32_t) length);
    p->in_length = 8 + length;
    p->in_at = 0;
}

ssize_t netlink_read(int handle, void *to, size_t length)
{
    int k = handle - HANDLE_BASE;
    if (k < 0 || k >= LINK_SLOTS) {
        return read(handle, to, length);
    }
    Peer *p = &nl.peer[k];
    if (p->in_at >= p->in_length) {
        next_message(k);
    }
    size_t have = (size_t) (p->in_length - p->in_at);
    length = length < have ? length : have;
    memcpy(to, p->in + p->in_at, length);
    p->in_at += (int) length;
    return (ssize_t) length;
}
