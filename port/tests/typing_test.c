/* Typing on the game's keyboard from another one (src/typing.c), against the game's keyboard
 * as the decomp has it, written out here a second time (the text it keeps, the syllable it
 * is putting together): no ROM is needed.
 *
 *     acww-typing-test
 *
 * A phone's Korean keyboard is played: for every letter typed, the syllable so far is taken
 * back and given anew, as SDL passes it on from Android. After each step the keys typing.c
 * asks for are given to the game here, and the game's text has to be the phone's.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/typing.h"

static int failures;

/* The game ---------------------------------------------------------------------------------- */

static uint16_t game_text[4096];
static int game_count, game_room = 4096;
static uint16_t held[8];
static int held_count;
static int only_common; /* the game knows only some syllables: here, those without a double final */

static int is_consonant(unsigned c)
{
    return c >= 0x3131 && c <= 0x314e;
}

static int is_vowel(unsigned c)
{
    return c >= 0x314f && c <= 0x3163;
}

static int game_knows(uint16_t s)
{
    static const int doubles[] = {3, 5, 6, 9, 10, 11, 12, 13, 14, 15, 18};
    if (only_common) {
        for (unsigned i = 0; i < sizeof(doubles) / sizeof(doubles[0]); i++) {
            if ((s - 0xac00) % 28 == doubles[i]) {
                return 0;
            }
        }
    }
    return 1;
}

static int game_gives(uint16_t c)
{
    return (c >= 0x3131 && c <= 0x3163) || (c >= 0xff01 && c <= 0xff5e) || c == 0x3000 || c == 0x000a;
}

/* func_ov095_0229b454 and _0229b704: the syllable of the keys, or 0 */
static uint16_t game_syllable(const uint16_t *src)
{
    static const char *const initials = "\x31\x32\x34\x37\x38\x39\x41\x42\x43\x45\x46\x47\x48\x49\x4a\x4b\x4c\x4d\x4e";
    static const char *const finals = "\x31\x32\x33\x34\x35\x36\x37\x39\x3a\x3b\x3c\x3d\x3e\x3f\x40\x41\x42\x44\x45\x46\x47\x48\x4a\x4b\x4c\x4d\x4e";
    uint16_t dst[3] = {0, 0, 0};
    int i = 2;
    if (!is_consonant(src[0]) || !is_vowel(src[1])) {
        return 0;
    }
    dst[0] = src[0];
    dst[1] = src[1];
    if (src[i] != 0 && is_vowel(src[i])) {
        unsigned a = dst[1], b = src[i];
        dst[1] = a == 0x3157 && b == 0x314f   ? 0x3158
                 : a == 0x3157 && b == 0x3150 ? 0x3159
                 : a == 0x3157 && b == 0x3163 ? 0x315a
                 : a == 0x315c && b == 0x3153 ? 0x315d
                 : a == 0x315c && b == 0x3154 ? 0x315e
                 : a == 0x315c && b == 0x3163 ? 0x315f
                 : a == 0x3161 && b == 0x3163 ? 0x3162
                                              : 0;
        if (dst[1] == 0) {
            return 0;
        }
        i++;
    }
    if (src[i] != 0) {
        if (!is_consonant(src[i]) || strchr(finals, (char) (src[i] & 0xff)) == NULL) {
            return 0;
        }
        dst[2] = src[i];
        if (src[i + 1] != 0) {
            unsigned a = dst[2], b = src[i + 1];
            dst[2] = a == 0x3131 && b == 0x3145   ? 0x3133
                     : a == 0x3134 && b == 0x3148 ? 0x3135
                     : a == 0x3134 && b == 0x314e ? 0x3136
                     : a == 0x3139 && b == 0x3131 ? 0x313a
                     : a == 0x3139 && b == 0x3141 ? 0x313b
                     : a == 0x3139 && b == 0x3142 ? 0x313c
                     : a == 0x3139 && b == 0x3145 ? 0x313d
                     : a == 0x3139 && b == 0x314c ? 0x313e
                     : a == 0x3139 && b == 0x314d ? 0x313f
                     : a == 0x3139 && b == 0x314e ? 0x3140
                     : a == 0x3142 && b == 0x3145 ? 0x3144
                                                  : 0;
            if (dst[2] == 0 || src[i + 2] != 0) {
                return 0;
            }
        }
    }
    const char *lead = strchr(initials, (char) (dst[0] & 0xff));
    if (lead == NULL) {
        return 0;
    }
    uint16_t s = (uint16_t) (0xac00 + ((lead - initials) * 21 + (dst[1] - 0x314f)) * 28 +
                             (dst[2] ? (int) (strchr(finals, (char) (dst[2] & 0xff)) - finals) + 1 : 0));
    return game_knows(s) ? s : 0;
}

/* a key (func_ov095_0229bd40 with _0229b784 and _0229b400), or 0x100: deleting backwards
   (func_ov095_0229b3a0 and the screens' own deletion). Returns 0 when the text has no room. */
static int game_key(unsigned key)
{
    if (key == 0x100) {
        if (held_count > 1) {
            held[--held_count] = 0;
            game_text[game_count - 1] = held_count > 1 ? game_syllable(held) : held[0];
        } else {
            memset(held, 0, sizeof(held));
            held_count = 0;
            if (game_count > 0) {
                game_count--;
            }
        }
        return 1;
    }
    int letter = is_consonant(key) || is_vowel(key), how = 0;
    uint16_t before = 0, now = (uint16_t) key;
    if (letter && held_count > 0) {
        uint16_t all[10] = {0};
        memcpy(all, held, sizeof(uint16_t) * 6);
        all[6] = 0;
        all[held_count] = (uint16_t) key;
        uint16_t s = game_syllable(all);
        if (s != 0) {
            how = 1;
            before = s;
        } else if ((s = game_syllable(&all[held_count - 1])) != 0) {
            now = s;
            all[held_count - 1] = 0;
            before = game_syllable(all);
            how = 2;
        }
    }
    if (how == 0 || how == 2) {
        if (game_count >= game_room) {
            return 0;
        }
        game_text[game_count++] = now;
    }
    if (how == 1) {
        game_text[game_count - 1] = before;
    } else if (how == 2) {
        game_text[game_count - 2] = before;
    }
    if (!letter) {
        memset(held, 0, sizeof(held));
        held_count = 0;
    } else if (how == 0) {
        memset(held, 0, sizeof(held));
        held[0] = (uint16_t) key;
        held_count = 1;
    } else if (how == 1) {
        held[held_count] = (uint16_t) key;
        if (held_count < 6) {
            held_count++;
        }
    } else {
        uint16_t last = held[held_count - 1];
        memset(held, 0, sizeof(held));
        held[0] = last;
        held[1] = (uint16_t) key;
        held_count = 2;
    }
    return 1;
}

/* The phone ---------------------------------------------------------------------------------- */

/* The phone's own keyboard puts syllables together as the game's does, and knows every one:
 * the game's keyboard here a second time, on a line of its own. */
static uint16_t line_text[256];
static int line_count;

static void line_key(uint16_t key, int first)
{
    static uint16_t keep_text[4096], keep_held[8], line_held[8];
    static int line_held_count;
    int keep_count = game_count, keep_held_count = held_count, keep_room = game_room, keep_only = only_common;
    memcpy(keep_text, game_text, sizeof(uint16_t) * (size_t) game_count);
    memcpy(keep_held, held, sizeof(held));
    if (first) {
        line_count = line_held_count = 0;
        memset(line_held, 0, sizeof(line_held));
    }
    memcpy(game_text, line_text, sizeof(uint16_t) * (size_t) line_count);
    memcpy(held, line_held, sizeof(held));
    game_count = line_count;
    held_count = line_held_count;
    game_room = 4096;
    only_common = 0;
    game_key(key);
    memcpy(line_text, game_text, sizeof(uint16_t) * (size_t) game_count);
    memcpy(line_held, held, sizeof(held));
    line_count = game_count;
    line_held_count = held_count;
    memcpy(game_text, keep_text, sizeof(uint16_t) * (size_t) keep_count);
    memcpy(held, keep_held, sizeof(held));
    game_count = keep_count;
    held_count = keep_held_count;
    game_room = keep_room;
    only_common = keep_only;
}

static uint32_t phone[4096]; /* its text, by code point */
static int phone_count;
static unsigned long keys_given;

static void utf8(uint32_t c, char *out)
{
    if (c < 0x80) {
        out[0] = (char) c, out[1] = 0;
    } else if (c < 0x800) {
        out[0] = (char) (0xc0 | c >> 6), out[1] = (char) (0x80 | (c & 0x3f)), out[2] = 0;
    } else {
        out[0] = (char) (0xe0 | c >> 12), out[1] = (char) (0x80 | (c >> 6 & 0x3f)), out[2] = (char) (0x80 | (c & 0x3f)), out[3] = 0;
    }
}

/* the phone's text becomes `now`: as SDL tells it, the characters after the common beginning
   taken back, then the new ones given */
static void phone_becomes(const uint32_t *now, int count)
{
    int same = 0;
    while (same < phone_count && same < count && phone[same] == now[same]) {
        same++;
    }
    for (int i = same; i < phone_count; i++) {
        typing_backspace();
    }
    for (int i = same; i < count; i++) {
        char bytes[8];
        utf8(now[i], bytes);
        typing_text(bytes);
    }
    memcpy(phone, now, sizeof(uint32_t) * (size_t) count);
    phone_count = count;
}

/* the keys typing.c asks for, given to the game until it asks for no more */
static int settle(void)
{
    for (int n = 0; n < 100000; n++) {
        uint32_t key = typing_next();
        if (key == 0) {
            return 1;
        }
        if (!game_key(key)) {
            return 0; /* no room: the session would begin anew */
        }
        typing_took();
        keys_given++;
        if (getenv("TYPING_TEST_KEYS") != NULL) {
            printf("  key %04x: the game has %d, the last %04x, holding %d\n", (unsigned) key, game_count,
                   game_count ? game_text[game_count - 1] : 0, held_count);
        }
    }
    return 0;
}

/* what the game's text should be for the phone's: its letters, digits and marks full width */
static int as_expected(const char *what)
{
    static uint16_t expected[4096];
    int n = 0, at = 0;
    for (int i = 0; i < phone_count; i++) {
        uint32_t c = phone[i];
        c = c == ' ' ? 0x3000 : c > 0x20 && c < 0x7f ? c + 0xfee0 : c;
        if (!game_gives((uint16_t) c) && !(c >= 0xac00 && c < 0xac00 + 11172)) {
            continue; /* no such key: left out */
        }
        expected[n++] = (uint16_t) c;
    }
    while (at < n && at < game_count && expected[at] == game_text[at]) {
        at++;
    }
    if (at == n && n == game_count) {
        return 1;
    }
    printf("FAILED: %s: from character %d the game has", what, at);
    for (int i = at > 2 ? at - 2 : 0; i < game_count && i < at + 8; i++) {
        printf(" %04x", game_text[i]);
    }
    printf(" (%d in all) for", game_count);
    for (int i = at > 2 ? at - 2 : 0; i < n && i < at + 8; i++) {
        printf(" %04x", expected[i]);
    }
    printf(" (%d in all)\n", n);
    failures++;
    return 0;
}

static void begin(void)
{
    static const TypingGame g = {game_gives, game_knows};
    typing_start(&g);
    game_count = held_count = phone_count = 0;
    memset(held, 0, sizeof(held));
    game_room = 4096;
}

/* A phone's two-set Korean keyboard: the letter keys pressed one by one; the text after each
 * is the syllables they make (the same rules, with every syllable known). */
static void phone_types(const char *what, const uint16_t *letters, int count, int check_each)
{
    static uint32_t now[4096];
    for (int i = 0; i < count; i++) {
        line_key(letters[i], i == 0);
        for (int k = 0; k < line_count; k++) {
            now[k] = line_text[k];
        }
        phone_becomes(now, line_count);
        if (!settle()) {
            printf("FAILED: %s: the keys do not end\n", what);
            failures++;
            return;
        }
        if (check_each && !as_expected(what)) {
            return;
        }
    }
}

static void check(int ok, const char *what)
{
    if (!ok) {
        printf("FAILED: %s\n", what);
        failures++;
    }
}

int main(void)
{
    /* 한글 입력 (the letters as a phone's keyboard has them) */
    static const uint16_t hangul[] = {0x314e, 0x314f, 0x3134, 0x3131, 0x3161, 0x3139, 0x3147, 0x3163, 0x3142, 0x3139, 0x3155, 0x3131};
    begin();
    keys_given = 0;
    phone_types("Hangul, letter by letter", hangul, 12, 1);
    check(game_count == 4 && game_text[0] == 0xd55c && game_text[1] == 0xae00 && game_text[2] == 0xc785 && game_text[3] == 0xb825, "한글입력");
    check(keys_given == 12, "a key for each letter typed, none typed twice");
    printf("%s: %lu keys for 12 letters\n", failures ? "FAILED" : "ok: Hangul, letter by letter", keys_given);

    /* the phone's backspace: a letter of the syllable being typed, then whole characters */
    {
        uint32_t t[8] = {0xd55c, 0xae00, 0xc785, 0xb824}; /* 한글입려 */
        phone_becomes(t, 4);
        check(settle() && as_expected("a letter taken back"), "a letter taken back");
        phone_becomes(t, 3);
        check(settle() && as_expected("a syllable taken back"), "a syllable taken back");
        phone_becomes(t, 1);
        check(settle() && as_expected("two characters taken back"), "two characters taken back");
        phone_becomes(t, 0);
        check(settle() && game_count == 0, "everything taken back");
    }

    /* a whole line given at once (pasted, or a word chosen from the phone's suggestions) */
    begin();
    {
        const char *line = "\xec\x95\x88\xeb\x85\x95 ABC abc 123!? \xea\xb0\x92\xec\x9d\x84 \xec\x9d\xbd\xeb\x8b\xa4\n";
        uint32_t t[64];
        int n = 0;
        typing_text(line);
        for (const unsigned char *p = (const unsigned char *) line; *p; n++) {
            if (*p < 0x80) {
                t[n] = *p++;
            } else {
                t[n] = (uint32_t) (p[0] & 0x0f) << 12 | (uint32_t) (p[1] & 0x3f) << 6 | (p[2] & 0x3f);
                p += 3;
            }
        }
        memcpy(phone, t, sizeof(uint32_t) * (size_t) n);
        phone_count = n;
        check(settle() && as_expected("a line at once"), "a line at once");
        printf("ok: a line at once (%d characters in the game)\n", game_count);
    }

    /* characters the game has no key for are left out */
    begin();
    typing_text("a\xf0\x9f\x98\x80\xe6\xbc\xa2z");
    check(settle() && game_count == 2 && game_text[0] == 0xff41 && game_text[1] == 0xff5a, "characters without a key left out");

    /* a game that knows only some syllables: the keys are followed as that game takes them */
    begin();
    only_common = 1;
    {
        static const uint16_t letters[] = {0x3137, 0x314f, 0x3139, 0x3131, 0x3131, 0x3157, 0x3131, 0x3163}; /* 닭 고 기 */
        phone_types("syllables the game does not know", letters, 8, 0);
        /* the phone has 닭고기; the game cannot make 닭 and has 달ㄱ고기: the same keys */
        check(game_count == 4 && game_text[0] == 0xb2ec && game_text[1] == 0x3131 && game_text[2] == 0xace0 && game_text[3] == 0xae30,
              "the same keys in a game without the syllable");
        uint32_t none[1] = {0};
        phone_becomes(none, 0);
        check(settle() && game_count == 0, "and taken back again, all of it");
    }
    only_common = 0;

    /* taking back what was there before the session */
    begin();
    game_key(0xff41);
    game_key(0xff42);
    typing_backspace();
    check(settle() && game_count == 1, "a character from before the session deleted");

    /* a long run by chance: letters typed, letters and characters taken back, syllables ended
       where the phone ends them (a word chosen, the cursor moved), other characters */
    begin();
    {
        static const uint16_t keys[] = {0x3131, 0x3134, 0x3137, 0x3139, 0x3141, 0x3142, 0x3145, 0x3147, 0x3148, 0x314e, 0x3132, 0x3146,
                                        0x314f, 0x3153, 0x3157, 0x315c, 0x3161, 0x3163, 0x3150, 0x3154};
        static uint32_t done[4096], now[4096];
        static uint16_t run[64];
        int done_count = 0, run_count = 0, steps = 0;
        unsigned seed = 12345;
        keys_given = 0;
        for (; steps < 20000 && failures == 0; steps++) {
            seed = seed * 1103515245u + 12345u;
            unsigned r = (seed >> 16) % 100;
            if (r < 62 && run_count < 40) {
                run[run_count++] = keys[(seed >> 8) % 20];
            } else if (r < 80) {
                if (run_count > 0) {
                    run_count--;
                } else if (done_count > 0) {
                    done_count--;
                }
            } else {
                /* the syllables typed so far are final; and sometimes another character follows */
                for (int k = 0; k < run_count; k++) {
                    line_key(run[k], k == 0);
                }
                for (int k = 0; run_count > 0 && k < line_count && done_count < 1600; k++) {
                    done[done_count++] = line_text[k];
                }
                run_count = 0;
                if (r >= 92 && done_count < 1600) {
                    done[done_count++] = "aZ 9!?~,."[(seed >> 8) % 9];
                }
                if (done_count > 1500) {
                    done_count = 0; /* (a session's text is kept shorter than that: typing_long) */
                }
            }
            for (int k = 0; k < run_count; k++) {
                line_key(run[k], k == 0);
            }
            int n = done_count;
            memcpy(now, done, sizeof(uint32_t) * (size_t) done_count);
            for (int k = 0; run_count > 0 && k < line_count; k++) {
                now[n++] = line_text[k];
            }
            if (getenv("TYPING_TEST_KEYS") != NULL) {
                printf("step %d (%u): the phone has %d, the last %04x %04x\n", steps, r, n, n > 1 ? (unsigned) now[n - 2] : 0,
                       n ? (unsigned) now[n - 1] : 0);
            }
            phone_becomes(now, n);
            if (!settle()) {
                check(0, "by chance: the keys do not end");
                break;
            }
            as_expected("typing and taking back by chance");
        }
        printf("%s: %d steps of typing and taking back by chance, %lu keys\n", failures ? "FAILED" : "ok", steps, keys_given);
    }

    printf(failures ? "typing-test: FAILED\n" : "typing-test: passed\n");
    return failures != 0;
}
