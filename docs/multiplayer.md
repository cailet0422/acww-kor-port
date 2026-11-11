<p align="center"><b>English</b> · <a href="multiplayer.ko.md">한국어</a></p>

# Playing together

What the DS does over its local wireless (opening the town's gate and visiting, moving to
another town, the passing exchange), between ports, over a network; and Nintendo Wi-Fi
Connection through the network the device is on, with the server chosen by DNS, which is the
way to players on real consoles (below: "Wi-Fi Connection").

## For players

Each player turns on "playing together" in the settings (`link=1`), gives a room's name, and
starts the app again; the wireless is on from the game's start. A room holds four consoles.

- **On one network** (the same Wi-Fi): one player leaves the server address empty. That
  device opens the room; the settings screen shows its address under "how it is running"
  (`room "..." opened here, 192.168.x.x:28920`). The others enter that address.
  `acww-kor --link-host` and `acww-kor --link-join 192.168.x.x` on a desktop.
- **Elsewhere**: everybody enters the address of a relay somebody runs on a machine all can
  reach: `acww-relay [PORT]` (built with the port; TCP, port 28920 unless another is given).
  It only passes messages on, and holds nothing.

Then play as with DSes side by side: one opens the gate for local wireless at the gatekeeper,
the others ask theirs to go out.

## How

The decomp's runtime has a model of the wireless manager (`decomp/runtime/wm.c`,
`decomp/docs/multi-console.md`): consoles are processes on one machine that hand each other,
once a frame, everything their radios sent in that frame, through pipes, and wait for each
other frame by frame. The decomp's scenes of two and three consoles (visits, the shops, the
museum, leaving) run on that.

The port keeps the model and replaces the pipes (`port/src/wm_port.c`, `netlink.c`):

- a frame's message to a console goes out at once over TCP, through the room's relay
  (`relay.c`, `link_proto.h`), numbered, with the count of that console's messages taken;
- a console's messages are taken in order, one a frame. When none has come a moment after
  it was due, the frame goes on with nothing heard, as a radio hears nothing for a moment,
  which the game's own protocol expects of the air; the stream is a frame later from then
  on, so the delay settles at what the journey needs;
- a console far ahead of what another has taken waits for it; one with many messages still
  to take takes two in a frame now and then;
- a console that leaves, or whose network goes, is reported to the model as far as it
  concerns it: a parent loses that child, a child its parent.

A console's number in the room (0 to 3, the relay's to give) is what the model makes its MAC
address of, so the room is joined before the game starts.

## Checked, and not

- `acww-netlink-test` (CI runs it): three consoles' links through a relay, without the
  game. Every message arrives once and in order; a slower console is waited for; a console
  that leaves is reported once.
- Two ports in a room on one machine, the game running in both at full speed with the
  wireless on; the new-game recording plays the same with the wireless on.
- **Not checked: a visit itself.** The game opens its wireless features after a player's
  first day, and the only towns at hand here are first-day ones. With two towns past that,
  the decomp's own two-console scenes are what to repeat over the network.

## Wi-Fi Connection

The game's Wi-Fi Connection is its own code from end to end: a TCP/IP stack, HTTP, SSL, the
servers' protocols. The decomp runs it against an access point, a network and servers that
it simulates inside the process. The port gives it the real network instead
(`port/src/wifi_port.c`), and what the game talks to is whatever answers there.

### For players

1. Turn on "Nintendo Wi-Fi Connection" in the settings (`wifi=1`) and enter the name server
   (DNS) of the server to play on (`wifi_dns=a.b.c.d`), then start the app again.
   `acww-kor --wifi-dns a.b.c.d` on a desktop. Nintendo's own servers closed in 2014; a
   private server is reached, as on a DS, by the DNS it tells its players to use. It has to
   be one that takes unpatched games by DNS alone (the game checks the server's certificate
   itself, with the ROM's own code).
2. The game finds an access point called `acww-kor-port`, and the console's first connection
   setting is already set for it. The title's "something else", "Wi-Fi settings" opens the
   game's settings program, where "connection test" tries the way out.
3. Then as on a console: friend codes at the gatekeeper, the gate opened for Wi-Fi.

"Playing together" (the room) and Wi-Fi Connection are not on at once: with both set, the
room is.

### How

- **The access point and the gateway.** The game sends whole IPv4 packets in 802.11 frames,
  which an app cannot put on the air. So behind the access point there is a small gateway
  of the port's own, the kind an emulator's user-mode network is: ARP and DHCP answered
  (the console is 192.168.11.2), name queries passed on to the chosen name server (or
  looked up by the device's resolver when none is chosen), a socket of the device's for each
  UDP port the console sends from, and each TCP connection of the console's ended at the
  gateway and carried on by one of the device's, with the sequence numbers, acknowledgements
  and windows the console's stack expects. HTTPS is passed as it is: the game's own SSL
  talks to the server.
- **The console's settings, from nothing.** The connection settings the game's library
  reads (three connections, in the firmware's memory below the user settings) are made by
  the port from the published layout: the first for the port's access point, the others
  unset. The console's address (MAC) is made once per installation, in Nintendo's block as
  a DS's is. The library writes the Wi-Fi user ID it is given by the server into those
  settings; the port keeps them in `wifi.bin` beside the save, so a town keeps its friend
  code. No firmware or NAND image is used or read.
- **The reset.** The settings program is a program of its own in the ROM, which the game
  starts by resetting the console (and comes back from the same way). The decomp's runtime
  stops there; the port's ARM7 (`port/src/arm7_port.c`) does the ARM7's part of a reset:
  the message back, the handshake on the SYNC lines while the ARM9 loads the programs from
  the card again, and a start as after power on, with the firmware's memory and the save
  chip as they are.

### Checked, and not

- `acww-wifi-test` (CI runs it): the gateway without the game, against servers on the
  loopback. The settings' checksums, the scan, ARP, DHCP, a name query through the chosen
  name server, UDP there and back, 300000 bytes each way over TCP with segments left out
  and sent twice, both ends' closing, a refused connection, every packet's checksums.
- The game's settings program from the title and back, with sound: the access point is
  found, the port's connection 1 is read as set, a new connection is saved (and is in
  `wifi.bin`), and its connection test, against test servers here, goes DHCP, the name
  query for `conntest.nintendowifi.net`, the HTTP request (answered 200), the name query for
  `nas.nintendowifi.net` and the game's SSL hello to port 443 (SSL 3.0, RC4). The test
  servers end there, and the game reports error 20100 (not the access point's or the
  network's kind: it got as far as the account server).
- **Not checked: a server.** No private server could be reached from where this was made.
  Whether the game's log-in, friend codes and a visit work is up to what the server answers
  and to the device's network (the matches are UDP between the players, through whatever
  NAT each is behind).

## Not yet

- **A server of the project's own for Wi-Fi Connection.** The relay above is the project's
  server for local wireless. For Wi-Fi Connection the port goes to the server the player
  names.
- **A real DS over local wireless.** Its local wireless is Nintendo's own protocol on 802.11,
  which a phone's Wi-Fi cannot speak; the way to play with one is Wi-Fi Connection, both on
  the same private server.
