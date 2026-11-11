/* Hooks into the game's own code as the interpreter runs it: places where the port changes
 * what the game does, by the decomp's addresses (the Korean release) and its readable
 * sources (decomp/clean/), which say what the code at each address is.
 *
 * What the game draws above its own screen, for the tall view (docs/tall-view.md), and whether
 * Mr. Resetti comes.
 */
#ifndef ACWW_PORT_GAME_HOOKS_H
#define ACWW_PORT_GAME_HOOKS_H

#include <stdint.h>

#include "arm.h"

/* Puts the port's hooks behind the run's own (run_port.c); call it once the run has started,
 * from the emulation's thread. */
void port_hooks_install(void);

/* The hook for a branch target, or NULL (run_port.c asks, after the run's own hooks). */
ArmHook game_hook_at(uint32_t address, void **user);

/* Every branch target is asked about, hundreds of thousands a frame, and nearly none has a
 * hook: a bit for each of 64 kinds of address (GAME_HOOK_KIND) says whether any address of
 * that kind has one now. */
extern uint64_t game_hook_kinds;
#define GAME_HOOK_KIND(address) (((address) >> 1) & 63u)

/* Whether objects above the lower screen are drawn (the tall view's upper part shows them). */
void game_hooks_draw_above(int on);

/* For the log: how many times an object was drawn that the game would have left out. */
unsigned long game_hooks_drawn_above(void);

/* Whether Mr. Resetti comes after a play that ended without a save (as on a DS), and how many
 * times he was kept away. */
void game_hooks_resetti(int on);
unsigned long game_hooks_resetti_kept_away(void);

#endif
