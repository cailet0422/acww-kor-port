/* The wireless manager's model (decomp/runtime/wm.c, as it is) for the front end: its link to
 * the other consoles, which the decomp's tools make of pipes between processes on one
 * machine, goes through the port's network link instead (netlink.h).
 *
 * wm.c reads and writes its link with read() and write() and nothing else with them; here
 * those two names are the port's functions, which take a console of the room for the file
 * descriptor. Its start is wrapped too, for the console's address. The headless test program
 * keeps the decomp's file.
 */
#include <stdint.h>
#include <unistd.h>

#include "netlink.h"

#define read netlink_read
#define write netlink_write
#define wm_init wm_init_decomp

int wm_init_decomp(uint8_t *nvram);

#include "wm.c"

#undef read
#undef write
#undef wm_init

#include "wifi_port.h"

/* The model's own start, then the console's address if the front end has one for it (for
 * Wi-Fi Connection, where every console needs its own: wifi_port.h). The model's is a fixed
 * address with the console's number in a room, which is right for a room. */
int wm_init(uint8_t *nvram)
{
    uint8_t mac[6];
    int on = wm_init_decomp(nvram);
    if (on && !wm.linked && wifi_port_mac(mac)) {
        memcpy(wm.mac, mac, 6);
        for (int i = 0; i < 6; i++) {
            wr8(SHARED_MAC_ADDRESS + (uint32_t) i, wm.mac[i]);
            nvram[0x36 + i] = wm.mac[i];
        }
    }
    return on;
}

/* The console's reset (arm7_port.c): the wireless as at the start. A console in a room keeps
 * its place there, and the model as it is: the game resets at its title, with the wireless
 * not in use. */
void wm_port_restart(uint8_t *nvram)
{
    if (wm.on && !wm.linked) {
        wm_init(nvram);
    }
}

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
