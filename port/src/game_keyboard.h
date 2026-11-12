/* The game's soft keyboard, typed on from outside (docs/typing.md): the hooks that tell when
 * a screen with the keyboard waits for the player, and that give a touched key the character
 * wanted. Part of the port's hooks into the game's code (game_hooks.h).
 */
#ifndef ACWW_PORT_GAME_KEYBOARD_H
#define ACWW_PORT_GAME_KEYBOARD_H

#include <stdint.h>

#include "arm.h"

/* The hooks on or off (off: the game's code runs untouched). */
void game_keyboard_enable(int on);

/* For game_hooks.c: the kinds of address hooked now, and the hook at an address. */
uint64_t game_keyboard_kinds(void);
ArmHook game_keyboard_hook_at(uint32_t address);

/* Whether a screen with the keyboard is up (the keyboard had its turn within the last few
 * frames). */
int game_keyboard_ready(void);

/* The next key the game asks the code of gives `code`: a character (the keyboard's own
 * codes are Unicode, Hangul as its letters, U+3131 and on), or 0x100 to delete backwards.
 * The key is then touched by the caller (KEYBOARD_TOUCH_X, _Y: the space bar, which every
 * page has). game_keyboard_taken says once whether the game has asked since; _let_go takes
 * the code back if it has not. */
#define KEYBOARD_TOUCH_X 144
#define KEYBOARD_TOUCH_Y 157
#define KEYBOARD_DELETE 0x100u
void game_keyboard_type(uint32_t code);
int game_keyboard_taken(void);
void game_keyboard_let_go(void);

/* Once each: whether the text had no room for the character last typed (asked a turn of the
 * game or two after it was taken), and whether a key of the keyboard's own was touched
 * since the last asking. */
int game_keyboard_refused(void);
int game_keyboard_foreign(void);

/* Whether one of the keyboard's keys gives this character, and whether the keyboard puts
 * this Hangul syllable together (it knows 2355 of them); from the game's tables in memory,
 * read when the keyboard is first seen. */
int game_keyboard_gives(uint16_t code);
int game_keyboard_syllable(uint16_t syllable);

/* For the log: how many characters were typed so. */
unsigned long game_keyboard_typed(void);

#endif
