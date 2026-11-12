/* typing.h */
#include "typing.h"

#include <string.h>

#define TEXT_MOST 2048
#define KEYS_MOST (TEXT_MOST * 5)
#define DELETE 0x100u
#define SPACE 0x3000u

static TypingGame game;

/* what the other keyboard's text is now (code points), since the session began */
static uint32_t text[TEXT_MOST];
static int text_count;
static int deletes; /* characters taken back that were not this session's: deleted as they are */

/* what the game has been given and not taken back: its keys, in the groups the game made
   characters of, and how many keys the game still holds to put more Hangul on (the last
   `composing` keys of the last group) */
static uint16_t sent[KEYS_MOST];
static int sent_count;
static int group_start[KEYS_MOST];
static int group_count;
static int composing;

/* the keys the text asks for (made anew when the text changes), and which of them begins a
   character */
static uint16_t want[KEYS_MOST];
static uint8_t want_begins[KEYS_MOST];
static int want_count, want_made;

static uint32_t given; /* the key typing_next gave last, until typing_took */

/* Hangul ------------------------------------------------------------------------------------ */

/* the letters as the game's keyboard has them (the compatibility jamo), by a syllable's parts */
static const uint16_t initial[19] = {0x3131, 0x3132, 0x3134, 0x3137, 0x3138, 0x3139, 0x3141, 0x3142, 0x3143, 0x3145,
                                     0x3146, 0x3147, 0x3148, 0x3149, 0x314a, 0x314b, 0x314c, 0x314d, 0x314e};
static const uint16_t final[28] = {0,      0x3131, 0x3132, 0x3133, 0x3134, 0x3135, 0x3136, 0x3137, 0x3139, 0x313a,
                                   0x313b, 0x313c, 0x313d, 0x313e, 0x313f, 0x3140, 0x3141, 0x3142, 0x3144, 0x3145,
                                   0x3146, 0x3147, 0x3148, 0x314a, 0x314b, 0x314c, 0x314d, 0x314e};
/* a letter made of two keys: the vowels, then the finals */
static const uint16_t pairs[][3] = {
    {0x3158, 0x3157, 0x314f}, {0x3159, 0x3157, 0x3150}, {0x315a, 0x3157, 0x3163}, {0x315d, 0x315c, 0x3153},
    {0x315e, 0x315c, 0x3154}, {0x315f, 0x315c, 0x3163}, {0x3162, 0x3161, 0x3163}, {0x3133, 0x3131, 0x3145},
    {0x3135, 0x3134, 0x3148}, {0x3136, 0x3134, 0x314e}, {0x313a, 0x3139, 0x3131}, {0x313b, 0x3139, 0x3141},
    {0x313c, 0x3139, 0x3142}, {0x313d, 0x3139, 0x3145}, {0x313e, 0x3139, 0x314c}, {0x313f, 0x3139, 0x314d},
    {0x3140, 0x3139, 0x314e}, {0x3144, 0x3142, 0x3145},
};
#define PAIRS ((int) (sizeof(pairs) / sizeof(pairs[0])))

static int is_consonant(uint32_t c)
{
    return c >= 0x3131 && c <= 0x314e;
}

static int is_vowel(uint32_t c)
{
    return c >= 0x314f && c <= 0x3163;
}

static uint16_t pair_of(uint16_t a, uint16_t b)
{
    for (int i = 0; i < PAIRS; i++) {
        if (pairs[i][1] == a && pairs[i][2] == b) {
            return pairs[i][0];
        }
    }
    return 0;
}

static int index_in(const uint16_t *list, int n, uint16_t c)
{
    for (int i = 0; i < n; i++) {
        if (list[i] == c && c != 0) {
            return i;
        }
    }
    return -1;
}

/* The syllable the game's keyboard makes of these keys, or 0: an initial, a vowel (of one key
 * or two), and perhaps a final (of one key or two), and one of the syllables it knows
 * (decomp/src/ov095/soft_keyboard_hangul.c). */
static uint16_t syllable_of(const uint16_t *k, int n)
{
    if (n < 2 || n > 5 || !is_consonant(k[0]) || !is_vowel(k[1])) {
        return 0;
    }
    int lead = index_in(initial, 19, k[0]), i = 2;
    uint16_t vowel = k[1], tail = 0;
    if (lead < 0) {
        return 0;
    }
    if (i < n && is_vowel(k[i])) {
        vowel = pair_of(vowel, k[i]);
        if (vowel == 0) {
            return 0;
        }
        i++;
    }
    if (i < n) {
        if (!is_consonant(k[i]) || index_in(final, 28, k[i]) < 0) {
            return 0;
        }
        tail = k[i++];
        if (i < n) {
            tail = is_consonant(k[i]) ? pair_of(tail, k[i]) : 0;
            if (tail == 0) {
                return 0;
            }
            i++;
        }
    }
    if (i != n) {
        return 0;
    }
    uint16_t s = (uint16_t) (0xac00 + (lead * 21 + (vowel - 0x314f)) * 28 + (tail ? index_in(final, 28, tail) : 0));
    return game.syllable == NULL || game.syllable(s) ? s : 0;
}

/* The keys a letter takes (one, or two for a letter made of two). */
static int letter_keys(uint16_t letter, uint16_t *out)
{
    for (int i = 0; i < PAIRS; i++) {
        if (pairs[i][0] == letter) {
            out[0] = pairs[i][1];
            out[1] = pairs[i][2];
            return 2;
        }
    }
    out[0] = letter;
    return 1;
}

/* The keys a character of the other keyboard's text takes on the game's: a Hangul syllable
 * its letters, everything else the game's own character for it (the game's letters, digits
 * and marks are the full-width ones), or none if the game's keyboard has no such key. */
static int keys_of(uint32_t c, uint16_t *out)
{
    if (c >= 0xac00 && c < 0xac00 + 11172) {
        uint32_t s = c - 0xac00;
        int n = 0;
        out[n++] = initial[s / (21 * 28)];
        n += letter_keys((uint16_t) (0x314f + s / 28 % 21), out + n);
        if (s % 28) {
            n += letter_keys(final[s % 28], out + n);
        }
        return n;
    }
    if (c >= 0x3131 && c <= 0x3163) {
        return letter_keys((uint16_t) c, out);
    }
    /* the letters as they are in the middle of a syllable being typed, on some keyboards */
    if (c >= 0x1100 && c <= 0x1112) {
        out[0] = initial[c - 0x1100];
        return 1;
    }
    if (c >= 0x1161 && c <= 0x1175) {
        return letter_keys((uint16_t) (0x314f + (c - 0x1161)), out);
    }
    if (c >= 0x11a8 && c <= 0x11c2) {
        return letter_keys(final[c - 0x11a8 + 1], out);
    }
    uint32_t tries[4] = {c, 0, 0, 0};
    int n = 1;
    if (c == ' ') {
        tries[0] = 0x3000;
    } else if (c > 0x20 && c < 0x7f) {
        tries[0] = c + 0xfee0; /* the full-width form */
        tries[n++] = c;
        if (c == '"') {
            tries[n++] = 0x201d;
        } else if (c == '\'') {
            tries[n++] = 0x2019;
        }
    } else if (c == 0x201c) {
        tries[n++] = 0x201d;
    } else if (c == 0x2018) {
        tries[n++] = 0x2019;
    } else if (c == 0xb7) {
        tries[n++] = 0x30fb;
    }
    for (int i = 0; i < n; i++) {
        if (tries[i] < 0x10000 && (game.gives == NULL || game.gives((uint16_t) tries[i]))) {
            out[0] = (uint16_t) tries[i];
            return 1;
        }
    }
    return 0;
}

static void make_want(void)
{
    want_count = 0;
    for (int i = 0; i < text_count && want_count + 5 <= KEYS_MOST; i++) {
        int n = keys_of(text[i], want + want_count);
        for (int k = 0; k < n; k++) {
            want_begins[want_count + k] = k == 0;
        }
        want_count += n;
    }
    want_made = 1;
}

/* The game as it takes a key (decomp/src/ov095/func_ov095_0229b784.c and soft_keyboard_text.c):
 * a letter is put on the syllable being typed if that makes a syllable; or, with the letter
 * before it, begins the next one (the final becomes the next initial); or begins a character
 * of its own. Anything else is a character of its own, and ends the syllable. */
static int joins(uint16_t key)
{
    if ((is_consonant(key) || is_vowel(key)) && composing > 0) {
        uint16_t held[8];
        int n = composing > 5 ? 5 : composing;
        memcpy(held, sent + sent_count - n, sizeof(uint16_t) * (size_t) n);
        held[n] = key;
        if (syllable_of(held, n + 1)) {
            return 1; /* on the syllable being typed */
        }
        if (n >= 2 && syllable_of(held + n - 1, 2)) {
            return 2; /* the next syllable, with the letter before it */
        }
    }
    return 0;
}

static void model_key(uint16_t key)
{
    int how = joins(key);
    if (how == 1) {
        sent[sent_count++] = key;
        composing++;
    } else if (how == 2) {
        group_start[group_count++] = sent_count - 1;
        sent[sent_count++] = key;
        composing = 2;
    } else {
        group_start[group_count++] = sent_count;
        sent[sent_count++] = key;
        composing = is_consonant(key) || is_vowel(key) ? 1 : 0;
    }
}

/* The game as it deletes backwards (func_ov095_0229b3a0.c): the last letter of the syllable
 * being typed while it has more than one, else the whole character before the cursor. */
static void model_delete(void)
{
    if (composing > 1) {
        sent_count--;
        composing--;
        return;
    }
    composing = 0;
    if (group_count > 0) {
        sent_count = group_start[--group_count];
    }
}

/* The session ------------------------------------------------------------------------------- */

void typing_start(const TypingGame *g)
{
    if (g != NULL) {
        game = *g;
    }
    text_count = deletes = 0;
    sent_count = group_count = composing = 0;
    want_count = 0;
    want_made = 1;
    given = 0;
}

void typing_text(const char *utf8)
{
    const unsigned char *p = (const unsigned char *) utf8;
    while (*p != 0) {
        uint32_t c = *p++;
        int more = c >= 0xf0 ? 3 : c >= 0xe0 ? 2 : c >= 0xc0 ? 1 : 0;
        c &= more == 3 ? 0x07 : more == 2 ? 0x0f : more == 1 ? 0x1f : 0x7f;
        for (; more > 0 && (*p & 0xc0) == 0x80; more--) {
            c = c << 6 | (*p++ & 0x3f);
        }
        if (c == '\r' || text_count >= TEXT_MOST) {
            continue;
        }
        text[text_count++] = c;
    }
    want_made = 0;
}

void typing_backspace(void)
{
    if (text_count > 0) {
        text_count--;
        want_made = 0;
    } else if (deletes < 64) {
        deletes++;
    }
}

/* How much of what the game has been given is as the text asks: the keys the same, and the
 * same keys beginning a character. (The game's characters can come apart from the text's
 * with the keys all the same: after 오허 the phone's backspace may leave 옿, and the game's
 * leaves 오ㅎ.) One difference is in order: a final consonant just typed sits on the syllable
 * before it until the vowel after it comes, which takes it along (하, 한, 하나). */
/* Whether the game would put the key at `at` of the keys asked for on the keys of its
 * character before it: not, when that is no syllable the game knows. */
static int joinable(int at)
{
    int from = at;
    while (from > 0 && !want_begins[from]) {
        from--;
    }
    return at - from + 1 <= 5 && syllable_of(want + from, at - from + 1) != 0;
}

static int as_asked(void)
{
    int same = 0, group = 0;
    while (same < sent_count && same < want_count && sent[same] == want[same]) {
        while (group < group_count && group_start[group] < same) {
            group++;
        }
        int begins = group < group_count && group_start[group] == same;
        if (begins != want_begins[same]) {
            int to_come = same == sent_count - 1 && want_begins[same] && sent_count < want_count && joins(want[sent_count]) == 2;
            int cannot = begins && !joinable(same); /* (the game does not know the syllable: its letters stay apart) */
            if (!to_come && !cannot) {
                /* back to where the text's character begins */
                while (same > 0 && !want_begins[same]) {
                    same--;
                }
                break;
            }
        }
        same++;
    }
    return same;
}

uint32_t typing_next(void)
{
    if (!want_made) {
        make_want();
    }
    int same = as_asked();
    if (sent_count > same) {
        given = DELETE;
    } else if (deletes > 0) {
        given = DELETE; /* (nothing of this session's is left in the game then) */
    } else if (sent_count < want_count && sent_count < KEYS_MOST - 1) {
        given = want[sent_count];
        if (want_begins[sent_count] && joins((uint16_t) given)) {
            /* The character begins anew in the other keyboard's text, and the game would put
               its first letter on the syllable before it (a syllable ended there without
               the game's knowing: 한 and then ㅏ, where the game would make 하나). A space
               ends the game's syllable; it is not in the text asked for, so it is deleted
               again next, and the letter then begins a character of its own. */
            given = SPACE;
        } else if (!want_begins[sent_count] && composing == 0 && sent_count > 0 && joinable(sent_count)) {
            /* The other way about: the letter belongs on the character before it, and the
               game is no longer putting letters on that one (a deletion ended it). The
               character is deleted, and typed anew with this letter. */
            given = DELETE;
        }
    } else {
        given = 0;
    }
    return given;
}

void typing_took(void)
{
    if (given == DELETE) {
        if (sent_count > 0) {
            model_delete();
        } else if (deletes > 0) {
            deletes--;
        }
    } else if (given != 0) {
        model_key((uint16_t) given);
    }
    given = 0;
}

int typing_pending(void)
{
    return typing_next() != 0;
}

int typing_count(void)
{
    return text_count;
}

int typing_long(void)
{
    return text_count > TEXT_MOST * 3 / 4;
}
