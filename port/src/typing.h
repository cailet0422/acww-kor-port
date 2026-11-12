/* Another keyboard's text, typed on the game's soft keyboard (docs/typing.md).
 *
 * A phone's keyboard says what its text is now: characters arrive, and are taken back again
 * (a Hangul syllable being typed is taken back and given anew, longer, at every letter). The
 * game's keyboard takes keys: Hangul by its letters, which it puts together itself, and
 * a key that deletes backwards. This keeps the two side by side: the text the other keyboard
 * has, the keys the game has been given, and the next key that brings the game's text nearer
 * to the other's: a letter more, or a deletion. What the game does with each key is followed
 * here exactly as the game does it (which keys it joins into a syllable, what a deletion
 * removes), so that a syllable grows by one key for each letter typed and is not typed anew.
 *
 * No SDL and nothing of the game in here: the game's part is asked through TypingGame.
 */
#ifndef ACWW_PORT_TYPING_H
#define ACWW_PORT_TYPING_H

#include <stdint.h>

typedef struct TypingGame {
    int (*gives)(uint16_t character);   /* one of the game keyboard's keys gives this character */
    int (*syllable)(uint16_t syllable); /* the game's keyboard puts this Hangul syllable together */
} TypingGame;

/* A session begins: the other keyboard's text is empty, and nothing in the game is this
 * session's. `game` may be NULL to keep the one given before. */
void typing_start(const TypingGame *game);

/* The other keyboard's text: characters added at its end (UTF-8), and its last character
 * taken back. A character taken back when the text is empty deletes in the game what was
 * there before the session. */
void typing_text(const char *utf8);
void typing_backspace(void);

/* The next key for the game: a character's code, 0x100 to delete backwards, or 0 when the
 * game's text is as the other keyboard's. The same key is given until typing_took says the
 * game has taken it. */
uint32_t typing_next(void);
void typing_took(void);

int typing_pending(void);
int typing_count(void); /* the characters of the other keyboard's text */

/* The session's text has grown long (there is room for some two thousand characters): time
 * to begin a new session, when nothing is pending. */
int typing_long(void);

#endif
