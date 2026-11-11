/* The port's own saving of the town, beside the game's (autosave.c, docs/autosave.md). */
#ifndef ACWW_PORT_AUTOSAVE_H
#define ACWW_PORT_AUTOSAVE_H

#include <stdint.h>

#define AUTOSAVE_CARD_SIZE 0x40000u /* the game's save memory: 256 KB */

/* Whether the game would let the player open its save menu at this moment (START), and a
 * town of this player's own is being played: the moments a save is taken at. `why`, when not
 * NULL, is given a word on what stands in the way (for the log). */
int autosave_quiet(const char **why);

/* The town as it is now, written as the game's save into `card`, an image of the save memory
 * as the game last wrote it: both copies of the state block, with the sums the game gives
 * them. The game's memory is only read. 0, or -1 when there is nothing to save. */
int autosave_snapshot(uint8_t *card);

#endif
