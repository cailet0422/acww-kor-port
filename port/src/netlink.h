/* The wireless of this console carried to the other consoles of a room over the network
 * (docs/multiplayer.md): the port's end of link_proto.h, and what the wireless manager's
 * model (decomp/runtime/wm.c, through wm_port.c) reads and writes in place of its pipes.
 */
#ifndef ACWW_PORT_NETLINK_H
#define ACWW_PORT_NETLINK_H

#include <stddef.h>
#include <sys/types.h>

/* Joins a room before the game starts. `server` is "host" or "host:port"; NULL or empty opens
 * the room on this machine (a relay on `port`, or the usual one when 0) and joins it. Returns
 * this console's number in the room, 0 to 3, or -1 with a word in `error` on why not. */
int netlink_start(const char *server, const char *room, int port, char *error, size_t error_size);

/* RUN_LINK for the run (wm.c): this console's number, four consoles, and for each other one
 * the handle netlink_read and netlink_write know it by. NULL before netlink_start. */
const char *netlink_run_link(void);

/* For the log and the settings screen: the room, this console's number, who is there. */
void netlink_status(char *text, size_t size);

/* What wm.c's read() and write() of its link are, in the front end's build (wm_port.c). A
 * handle that is not a console's is passed to the real read() and write(). */
ssize_t netlink_read(int handle, void *to, size_t length);
ssize_t netlink_write(int handle, const void *from, size_t length);

/* wm_port.c: the wireless message that tells this console that console `console` of the
 * room is gone, as its state is now (a child lost, or the parent); 0 when it does not concern
 * it. `out` has room for 16 bytes. */
int wm_port_lost_record(int console, unsigned char *out);

#endif
