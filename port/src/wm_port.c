/* The wireless manager's model (decomp/runtime/wm.c, as it is) for the front end: its link to
 * the other consoles, which the decomp's tools make of pipes between processes on one
 * machine, goes through the port's network link instead (netlink.h).
 *
 * wm.c reads and writes its link with read() and write() and nothing else with them; here
 * those two names are the port's functions, which take a console of the room for the file
 * descriptor. The headless test program keeps the decomp's file.
 */
#include <unistd.h>

#include "netlink.h"

#define read netlink_read
#define write netlink_write

#include "wm.c"

#undef read
#undef write

/* A console of the room went away without a word (its network did). What this console is
 * told is what wm.c takes for a disconnection (MSG_DISCONNECT, as its `receive` reads it):
 * as a parent, its child of that MAC and AID is gone; as that console's child, its own MAC
 * and AID, which is the parent's sending it away, taken as the parent lost. */
int wm_port_lost_record(int console, unsigned char *out)
{
    uint8_t mac[6], m[8];
    int concerns = 0;
    if (!wm.on || !wm.linked) {
        return 0;
    }
    memcpy(mac, wm.mac, 6);
    mac[5] = (uint8_t) (wm.mac[5] - wm.console + console);
    if (wm.parent) {
        for (uint32_t aid = 1; aid <= MAX_CHILDREN && !concerns; aid++) {
            if ((wm.child_bitmap & (1u << aid)) && memcmp(wm.children[aid], mac, 6) == 0) {
                memcpy(m, mac, 6);
                put16(m + 6, aid);
                concerns = 1;
            }
        }
    } else if ((wm.connected || wm.connecting) && !wm.infra && memcmp(wm.parent_mac, mac, 6) == 0) {
        memcpy(m, wm.mac, 6);
        put16(m + 6, wm.aid);
        concerns = 1;
    }
    if (!concerns) {
        return 0;
    }
    out[0] = MSG_DISCONNECT;
    put16(out + 1, sizeof(m));
    memcpy(out + 3, m, sizeof(m));
    return 3 + (int) sizeof(m);
}
