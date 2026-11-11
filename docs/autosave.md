<p align="center"><b>English</b> · <a href="autosave.ko.md">한국어</a></p>

# Autosave and Mr. Resetti

## What the game's save is

The game keeps the whole town in one block of memory, the state block (`data_021dc7a8`,
0x173fc bytes; `decomp/clean/main/state_block.h`). Its save is that block written to the card
as it is, twice, with sums: the parts' own (the exchange block, the waiting letter, each town
player's Wi-Fi account and friend roster) and the block's, kept in a stamp at its end with a
serial that tells a save written whole from one cut short
(`activity_card.c`, `ActivityCard_WriteStepSums`). Nothing is gathered before the write.

## The port's own save

So a save can be made without the game. `port/src/autosave.c` copies the block out of the
DS's memory, gives it the same sums, and puts it where the game's two copies are in an image
of the card. The game's memory is only read: the game runs as if nothing happened, and its
frames stay the decomp's.

- **Where.** `acww.sav.auto`, beside the save. The game's own save file is never touched.
- **When.** Every minute, and when the app goes out of sight, but only at a moment the game
  itself would let the player open its save menu (START): the same conditions its START
  button's process checks (`start_menu.c`: no event under way, the field taking input, not a
  new player's first day, on which the game says it cannot save yet), in a town of the
  player's own and outside a session with other systems.
- **The game saved.** Then its save is the newer one, and the autosave is taken away.
- **The next start.** If there is an autosave, the player is asked which to go on from
  (Android), or `--load-autosave` says so. Going on from it makes it the save; the save it
  replaces is kept as `acww.sav.before-auto`.
- **Mr. Resetti.** The autosave counts as saved: the flag that brings him after a play that
  ended without a save is cleared in it.

Off with "autosave" on the settings screen, `--no-autosave`, or `autosave=0` in
`settings.txt`.

### Checked

With the decomp's new-game recording, played on by script past the shopkeeper's first talk:

- during the first day no autosave is made, and the game's own menu says "you cannot save
  yet" at the same moments;
- an autosave differs from the save the game itself had written in 5 bytes of 95228: the
  flag, and the stamp's serial and sum. Every other byte, the parts' sums included, is the
  game's;
- started from it, the game offers to continue, loads the town, and the player stands in
  front of the house as after a save;
- the game's own "save and stop" still works, and takes the autosave away.

Not checked: a town played for days, with letters, visitors and several players.

## Mr. Resetti on and off

A town that is loaded gets the flag "not saved" (`PLAYER_FLAG_UNSAVED`), written to the card
at once; saving and stopping clears it. Found set at a load, it brings Mr. Resetti
(`state_block.c`). `port/src/game_hooks.c` answers that one test with "saved" unless the
player wants him (`--resetti`, the settings screen).

Checked with a play that ended without a save: with him on, he comes up when the player
walks from the house; with him off, the same walk goes undisturbed.
