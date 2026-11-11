/* The relay of the rooms consoles meet in (link_proto.h, docs/multiplayer.md): the server a
 * port connects to for playing with others. It runs by itself (acww-relay), or inside a port
 * that opens a room for consoles on the same network. */
#ifndef ACWW_PORT_RELAY_H
#define ACWW_PORT_RELAY_H

/* Listens on `port` (all addresses) and serves until *stop is nonzero (looked at about once
 * a second; NULL: for ever). 0 at the end, -1 when it could not listen. `log`, when not
 * NULL, gets a line for each console that comes and goes. */
int relay_run(int port, const volatile int *stop, void (*log)(const char *line));

/* The same on a thread of its own. 0 once it listens, -1 when it cannot. */
int relay_start(int port);

#endif
