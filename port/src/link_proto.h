/* What goes between a port and the relay of a room (relay.c, netlink.c, docs/multiplayer.md).
 *
 * A room is up to four consoles that share the air as consoles side by side do: what one's
 * wireless sends, the others hear. The relay only passes it on. Over TCP, everything in
 * packets of a four-byte head and a body:
 *
 *     kind (1 byte), slot (1 byte), the body's length (2 bytes, little endian)
 *
 *   HELLO    port to relay, first: the protocol's version (1 byte), then the room's name
 *   WELCOME  relay to port: `slot` is the console's number in the room, 0 to 3; the body is
 *            one byte, the slots taken (a bit each)
 *   REFUSED  relay to port: the room is full or the hello was not understood; then it hangs up
 *   MEMBERS  relay to port, whenever it changes: one byte, the slots taken
 *   DATA     port to relay with `slot` the console it is for; relay to port with `slot` the
 *            console it is from. The body is the port's own (netlink.c): a frame's wireless
 *            messages, numbered and acknowledged
 *   PING     port to relay, any body; PONG comes back with the same body
 */
#ifndef ACWW_PORT_LINK_PROTO_H
#define ACWW_PORT_LINK_PROTO_H

#define LINK_VERSION 1
#define LINK_SLOTS 4
#define LINK_PORT 28920        /* the relay's port unless another is named */
#define LINK_ROOM_NAME 32      /* bytes of a room's name, at most */
#define LINK_BODY_MAX 8192     /* of a packet */

enum { LINK_HELLO = 1, LINK_WELCOME = 2, LINK_REFUSED = 3, LINK_MEMBERS = 4, LINK_DATA = 5, LINK_PING = 6, LINK_PONG = 7 };

#endif
