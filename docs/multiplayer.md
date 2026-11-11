<p align="center"><b>English</b> · <a href="multiplayer.ko.md">한국어</a></p>

# Playing together

What the DS does over its local wireless (opening the town's gate and visiting, moving to
another town, the passing exchange), between ports, over a network. Not yet: Nintendo Wi-Fi
Connection, and a real DS (below).

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

## Not yet

- **Nintendo Wi-Fi Connection.** The decomp simulates an access point, a network and the
  servers inside the process, for the game's own code to run against. For the real thing the
  port needs a way from the game's own TCP/IP to real sockets, the console's connection
  settings (with the DNS a private server is reached by) made by the port itself, and the
  servers' answers left to that server.
- **A real DS.** Its local wireless is Nintendo's own protocol on 802.11, which a phone's
  Wi-Fi cannot speak; the way to play with one is Wi-Fi Connection through a private server.
