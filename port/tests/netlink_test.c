/* The network link (port/src/netlink.c) and the relay (relay.c) without the game: three
 * consoles in a room, each a process that writes a message a frame for each other console
 * and reads one from each, as the wireless manager's model does (wm.c). A message holds a
 * numbered record. Checked: every console gets every other's records in order, none lost and
 * none twice, whatever their pace (one console runs slower than the others); and when a
 * console goes away, the others are told once and go on.
 *
 *     acww-netlink-test [PORT [SLOW_MS]]
 */
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "link_proto.h"
#include "netlink.h"
#include "relay.h"

#define CONSOLES 3
#define FRAMES 600
#define LEAVES_AT 300   /* the frame console 2 goes away at */
#define RECORD 0x7e     /* a test record: its sender and its number */
#define RECORD_LOST 0x7f

static uint32_t frame_now;

/* what netlink.c asks of the runtime and of wm_port.c */
uint32_t io_frame(void)
{
    return frame_now;
}

int wm_port_lost_record(int console, unsigned char *out)
{
    out[0] = RECORD_LOST;
    out[1] = 1;
    out[2] = 0;
    out[3] = (unsigned char) console;
    return 4;
}

static void put32(uint8_t *p, uint32_t v)
{
    memcpy(p, &v, 4);
}

static uint32_t get32(const uint8_t *p)
{
    uint32_t v;
    memcpy(&v, p, 4);
    return v;
}

static void nap(int ms)
{
    struct timespec t = {0, ms * 1000000L};
    nanosleep(&t, NULL);
}

/* One console: 0 when all it took was in order. */
static int console(int port, int pace_ms, int leaves)
{
    char server[64], error[160], status[320];
    int handles[LINK_SLOTS], bad = 0;
    uint32_t next[LINK_SLOTS] = {0}, told[LINK_SLOTS] = {0}, sent = 0;
    snprintf(server, sizeof(server), "127.0.0.1:%d", port);
    int slot = netlink_start(server, "test", 0, error, sizeof(error));
    if (slot < 0) {
        fprintf(stderr, "console: %s\n", error);
        return 1;
    }
    /* the handles, from RUN_LINK as wm.c reads it: "<console>,<consoles>,<in>,<out>,..." */
    const char *p = strchr(strchr(netlink_run_link(), ',') + 1, ',');
    for (int k = 0; k < LINK_SLOTS; k++) {
        handles[k] = -1;
        if (k != slot) {
            int in = 0, used = 0;
            sscanf(p, ",%d,%*d%n", &in, &used);
            handles[k] = in;
            p += used;
        }
    }
    nap(300); /* until the others are in the room */
    for (frame_now = 1; frame_now <= FRAMES; frame_now++) {
        if (leaves && frame_now == LEAVES_AT) {
            _exit(0);
        }
        for (int k = 0; k < LINK_SLOTS; k++) {
            if (handles[k] < 0) {
                continue;
            }
            uint8_t out[8 + 3 + 8], in[8 + 8192];
            put32(out, frame_now);
            put32(out + 4, 3 + 8);
            out[8] = RECORD;
            out[9] = 8;
            out[10] = 0;
            put32(out + 11, (uint32_t) slot);
            put32(out + 15, sent);
            netlink_write(handles[k], out, 8);
            netlink_write(handles[k], out + 8, 3 + 8);
            if (netlink_read(handles[k], in, 8) != 8 || get32(in) != frame_now || get32(in + 4) > 8192) {
                bad++;
                continue;
            }
            uint32_t length = get32(in + 4);
            if (length != 0 && netlink_read(handles[k], in + 8, length) != (ssize_t) length) {
                bad++;
                continue;
            }
            for (uint32_t n = 0; n + 3 <= length;) {
                uint32_t size = in[8 + n + 1] | (uint32_t) in[8 + n + 2] << 8;
                if (in[8 + n] == RECORD && size == 8) {
                    /* console k's records come as it numbered them, each once */
                    bad += get32(in + 8 + n + 3) != (uint32_t) k || get32(in + 8 + n + 7) != next[k];
                    next[k]++;
                } else if (in[8 + n] == RECORD_LOST) {
                    told[k]++;
                } else {
                    bad++;
                }
                n += 3 + size;
            }
        }
        sent++;
        nap(pace_ms);
    }
    netlink_status(status, sizeof(status));
    printf("console %d: took", slot);
    for (int k = 0; k < LINK_SLOTS; k++) {
        if (handles[k] >= 0) {
            printf(" %u of console %d%s", next[k], k, told[k] == 1 ? " (told it left)" : told[k] ? " (TOLD TWICE)" : "");
            bad += told[k] > 1;
        }
    }
    printf("; out of order or lost: %d; %s\n", bad, status);
    fflush(stdout);
    /* the console that left sent LEAVES_AT - 1 records and was reported gone; the others'
       all but the last few, still on their way when this one stopped */
    for (int k = 0; k < CONSOLES; k++) {
        if (k != slot && k == 2) {
            bad += told[k] != 1 || next[k] > LEAVES_AT - 1 || next[k] < LEAVES_AT - 1 - 80;
        } else if (k != slot) {
            bad += next[k] < FRAMES - 80;
        }
    }
    return bad != 0;
}

int main(int argc, char **argv)
{
    int port = argc > 1 ? atoi(argv[1]) : 28990, failed = 0;
    int slow = argc > 2 ? atoi(argv[2]) : 6; /* the slow console's pause a frame, in ms */
    pid_t relay = fork();
    if (relay == 0) {
        relay_run(port, NULL, NULL);
        _exit(1);
    }
    nap(200);
    pid_t pids[CONSOLES];
    for (int i = 0; i < CONSOLES; i++) {
        pids[i] = fork();
        if (pids[i] == 0) {
            /* the first two keep the DS's pace, about; the one that leaves is slower, so the
               others are ahead of it and have to wait for it */
            _exit(console(port, i == 2 ? slow : 3, i == 2));
        }
        nap(60); /* in order: the consoles' numbers are 0, 1, 2 */
    }
    for (int i = 0; i < CONSOLES; i++) {
        int status = 0;
        waitpid(pids[i], &status, 0);
        failed += !WIFEXITED(status) || WEXITSTATUS(status) != 0;
    }
    kill(relay, SIGTERM);
    waitpid(relay, NULL, 0);
    printf(failed ? "FAILED: %d consoles\n" : "ok: three consoles through the relay, one leaving\n", failed);
    return failed != 0;
}
