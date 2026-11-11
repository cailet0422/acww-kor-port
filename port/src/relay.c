/* The relay of the rooms consoles meet in (relay.h, link_proto.h).
 *
 * One thread, poll() over the listening socket and the consoles'. A console says hello with a
 * room's name and gets the lowest free slot of that room (a room is made by its first
 * console and gone with its last). DATA for a slot is passed to the console in that slot of
 * the same room, marked with the sender's slot. Nothing is kept, checked or understood beyond
 * that. A console that sends faster than another takes in is not waited for more than a
 * moment: the slow one's socket is closed, which both ends treat as a console gone.
 */
#include "relay.h"

#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "link_proto.h"

#define CLIENTS 256
#define OUT_ROOM (1 << 17) /* bytes that may wait to be sent to one console */

typedef struct Client {
    int fd;
    int slot;                      /* -1 before the hello */
    char room[LINK_ROOM_NAME + 1];
    uint8_t in[4 + LINK_BODY_MAX]; /* the packet arriving */
    int in_length;
    uint8_t *out;                  /* what could not be sent at once */
    int out_length;
} Client;

static Client clients[CLIENTS];
static uint8_t out_store[CLIENTS][OUT_ROOM];

static void say(void (*log)(const char *), const char *format, const char *room, int slot, int count)
{
    char line[160];
    if (log != NULL) {
        snprintf(line, sizeof(line), format, room, slot, count);
        log(line);
    }
}

static void drop(Client *c)
{
    if (c->fd >= 0) {
        close(c->fd);
    }
    c->fd = -1;
    c->slot = -1;
    c->in_length = c->out_length = 0;
}

/* Queues a packet for a console; one that cannot take it is dropped. */
static void send_packet(Client *c, int kind, int slot, const uint8_t *body, int length)
{
    uint8_t head[4] = {(uint8_t) kind, (uint8_t) slot, (uint8_t) length, (uint8_t) (length >> 8)};
    if (c->fd < 0) {
        return;
    }
    if (c->out_length + 4 + length > OUT_ROOM) {
        drop(c);
        return;
    }
    memcpy(c->out + c->out_length, head, 4);
    memcpy(c->out + c->out_length + 4, body, (size_t) length);
    c->out_length += 4 + length;
}

static void flush(Client *c)
{
    while (c->fd >= 0 && c->out_length > 0) {
        ssize_t done = send(c->fd, c->out, (size_t) c->out_length, MSG_NOSIGNAL);
        if (done < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
            return;
        }
        if (done <= 0) {
            drop(c);
            return;
        }
        memmove(c->out, c->out + done, (size_t) (c->out_length - done));
        c->out_length -= (int) done;
    }
}

static int members_of(const char *room, int *count)
{
    int mask = 0;
    *count = 0;
    for (int i = 0; i < CLIENTS; i++) {
        if (clients[i].fd >= 0 && clients[i].slot >= 0 && strcmp(clients[i].room, room) == 0) {
            mask |= 1 << clients[i].slot;
            (*count)++;
        }
    }
    return mask;
}

static void tell_members(const char *room)
{
    int count;
    uint8_t mask = (uint8_t) members_of(room, &count);
    for (int i = 0; i < CLIENTS; i++) {
        if (clients[i].fd >= 0 && clients[i].slot >= 0 && strcmp(clients[i].room, room) == 0) {
            send_packet(&clients[i], LINK_MEMBERS, clients[i].slot, &mask, 1);
        }
    }
}

static void leave(Client *c, void (*log)(const char *))
{
    char room[LINK_ROOM_NAME + 1];
    int slot = c->slot, count;
    memcpy(room, c->room, sizeof(room));
    drop(c);
    if (slot >= 0) {
        members_of(room, &count);
        say(log, "relay: room \"%s\": console %d left (%d there)", room, slot, count);
        tell_members(room);
    }
}

/* A whole packet from a console. */
static void packet(Client *c, void (*log)(const char *))
{
    int kind = c->in[0], slot = c->in[1], length = c->in[2] | c->in[3] << 8, count;
    const uint8_t *body = c->in + 4;
    if (c->slot < 0) {
        /* the hello: the version, then the room's name */
        if (kind != LINK_HELLO || length < 1 || length > 1 + LINK_ROOM_NAME || body[0] != LINK_VERSION) {
            send_packet(c, LINK_REFUSED, 0, (const uint8_t *) "hello", 5);
            flush(c);
            drop(c);
            return;
        }
        memset(c->room, 0, sizeof(c->room));
        memcpy(c->room, body + 1, (size_t) length - 1);
        int mask = members_of(c->room, &count);
        for (int s = 0; s < LINK_SLOTS && c->slot < 0; s++) {
            if (!(mask & (1 << s))) {
                c->slot = s;
            }
        }
        if (c->slot < 0) {
            send_packet(c, LINK_REFUSED, 0, (const uint8_t *) "full", 4);
            flush(c);
            drop(c);
            return;
        }
        uint8_t now = (uint8_t) (mask | 1 << c->slot);
        send_packet(c, LINK_WELCOME, c->slot, &now, 1);
        say(log, "relay: room \"%s\": console %d came (%d there)", c->room, c->slot, count + 1);
        tell_members(c->room);
        return;
    }
    if (kind == LINK_PING) {
        send_packet(c, LINK_PONG, c->slot, body, length);
    } else if (kind == LINK_DATA && slot < LINK_SLOTS && slot != c->slot) {
        for (int i = 0; i < CLIENTS; i++) {
            if (clients[i].fd >= 0 && clients[i].slot == slot && strcmp(clients[i].room, c->room) == 0) {
                send_packet(&clients[i], LINK_DATA, c->slot, body, length);
            }
        }
    }
}

static void receive(Client *c, void (*log)(const char *))
{
    for (;;) {
        int want = c->in_length < 4 ? 4 : 4 + (c->in[2] | c->in[3] << 8);
        if (want > (int) sizeof(c->in)) {
            leave(c, log);
            return;
        }
        if (c->in_length == want) {
            packet(c, log);
            if (c->fd < 0) {
                return;
            }
            c->in_length = 0;
            continue;
        }
        ssize_t done = recv(c->fd, c->in + c->in_length, (size_t) (want - c->in_length), 0);
        if (done < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
            return;
        }
        if (done <= 0) {
            leave(c, log);
            return;
        }
        c->in_length += (int) done;
    }
}

static int listen_on(int port)
{
    int fd = socket(AF_INET6, SOCK_STREAM, 0), one = 1, zero = 0;
    struct sockaddr_in6 six;
    memset(&six, 0, sizeof(six));
    six.sin6_family = AF_INET6;
    six.sin6_port = htons((uint16_t) port);
    if (fd >= 0) {
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &zero, sizeof(zero)); /* IPv4 too */
        if (bind(fd, (struct sockaddr *) &six, sizeof(six)) == 0 && listen(fd, 16) == 0) {
            return fd;
        }
        close(fd);
    }
    /* a machine without IPv6 */
    struct sockaddr_in four;
    memset(&four, 0, sizeof(four));
    four.sin_family = AF_INET;
    four.sin_port = htons((uint16_t) port);
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    if (bind(fd, (struct sockaddr *) &four, sizeof(four)) != 0 || listen(fd, 16) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static int serve(int listening, const volatile int *stop, void (*log)(const char *))
{
    for (int i = 0; i < CLIENTS; i++) {
        clients[i].fd = -1;
        clients[i].slot = -1;
        clients[i].out = out_store[i];
    }
    while (stop == NULL || !*stop) {
        struct pollfd fds[CLIENTS + 1];
        int who[CLIENTS + 1], count = 1;
        fds[0] = (struct pollfd){listening, POLLIN, 0};
        for (int i = 0; i < CLIENTS; i++) {
            if (clients[i].fd >= 0) {
                who[count] = i;
                fds[count++] = (struct pollfd){clients[i].fd, (short) (POLLIN | (clients[i].out_length > 0 ? POLLOUT : 0)), 0};
            }
        }
        if (poll(fds, (nfds_t) count, 1000) < 0 && errno != EINTR) {
            break;
        }
        for (int k = 1; k < count; k++) {
            Client *c = &clients[who[k]];
            if (c->fd >= 0 && (fds[k].revents & (POLLIN | POLLHUP | POLLERR))) {
                receive(c, log);
            }
        }
        if (fds[0].revents & POLLIN) {
            int fd = accept(listening, NULL, NULL), one = 1;
            Client *c = NULL;
            for (int i = 0; i < CLIENTS && c == NULL; i++) {
                c = clients[i].fd < 0 ? &clients[i] : NULL;
            }
            if (fd >= 0 && c != NULL) {
                fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
                setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
                c->fd = fd;
                c->slot = -1;
                c->in_length = c->out_length = 0;
            } else if (fd >= 0) {
                close(fd);
            }
        }
        /* what was queued in this round goes out now, not at the next */
        for (int i = 0; i < CLIENTS; i++) {
            if (clients[i].fd >= 0 && clients[i].out_length > 0) {
                flush(&clients[i]);
            }
        }
    }
    for (int i = 0; i < CLIENTS; i++) {
        drop(&clients[i]);
    }
    close(listening);
    return 0;
}

int relay_run(int port, const volatile int *stop, void (*log)(const char *line))
{
    int listening = listen_on(port);
    if (listening < 0) {
        return -1;
    }
    return serve(listening, stop, log);
}

static void *relay_thread(void *arg)
{
    serve((int) (intptr_t) arg, NULL, NULL);
    return NULL;
}

int relay_start(int port)
{
    pthread_t thread;
    int listening = listen_on(port);
    if (listening < 0) {
        return -1;
    }
    if (pthread_create(&thread, NULL, relay_thread, (void *) (intptr_t) listening) != 0) {
        close(listening);
        return -1;
    }
    pthread_detach(thread);
    return 0;
}
