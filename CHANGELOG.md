<p align="center"><b>English</b> · <a href="CHANGELOG.ko.md">한국어</a></p>

# Changes

What changed in each version. A release's description, and what the app shows when it offers
an update, are taken from here ([docs/releases.md](docs/releases.md)).

## 0.14.0

- **Typing with the phone's keyboard**: where the game shows its own keyboard (names,
  letters, the board, chat), the phone's keyboard types. Hangul goes to the game's keyboard
  letter by letter and the game puts it together; Latin letters, digits and marks become the
  game's full-width ones. It can be turned off under Settings > Controls.
- While typing, the lower screen, which the phone's keyboard covers, is shown at the top of
  the display. The game's buttons there (Decide and the others) are touched where they are
  shown.
- If the phone's keyboard is put away, a touch on the text brings it back.

Tried on the name entry only: the letter, the board and the chat, and a real phone's
keyboard, have not been tried yet.

## 0.13.0

The first release.

- **Updates**: when the app starts it asks the project's releases on GitHub for a newer
  version and offers one at the next start. "Update" fetches and installs it; the save and
  the settings stay. It can be turned off, or asked at once, under Settings > About.
- **Sixty frames**: scenes made between the game's twenty a second, for sixty pictures a
  second (with the Vulkan renderer). What made the animals flicker while the player walked,
  and the motion uneven, is fixed.
- What there is so far: the Vulkan renderer (on by default), the tall view, full-touch play
  and the buttons' arrangement, the port's own save, Resetti on or off, playing together
  (the local wireless over the network), Nintendo Wi-Fi Connection (the servers a chosen DNS
  names).

Not yet tried on a device: Wi-Fi Connection against a real private server, a visit between
two devices.

No game data is included. A dump of the Korean release's ROM (`ADMK`) is needed.
