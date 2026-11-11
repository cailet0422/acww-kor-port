/* acww-relay [PORT]: the relay of the rooms ports meet in, by itself (port/src/relay.h,
 * docs/multiplayer.md). Run it on a machine the players can reach; they give its address and a
 * room's name in their settings. */
#include <stdio.h>
#include <stdlib.h>

#include "link_proto.h"
#include "relay.h"

static void log_line(const char *line)
{
    printf("%s\n", line);
    fflush(stdout);
}

int main(int argc, char **argv)
{
    int port = argc > 1 ? atoi(argv[1]) : LINK_PORT;
    printf("acww-relay: rooms on port %d\n", port);
    fflush(stdout);
    if (relay_run(port, NULL, log_line) != 0) {
        fprintf(stderr, "acww-relay: cannot listen on port %d\n", port);
        return 1;
    }
    return 0;
}
