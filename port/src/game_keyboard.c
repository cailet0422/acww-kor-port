/* game_keyboard.h */
#include "game_keyboard.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "io.h"
#include "memory.h"

#include "game_hooks.h"

/* The soft keyboard (overlay 95 of the Korean release; decomp/src/ov095/soft_keyboard*.c).
 *
 * The four screens that use it (the name and text entry, the chat, the board's writing and
 * the letter's) work alike: each turn of the game they let the keyboard do its own turn
 * (func_ov095_0229bf7c), and when the pen comes down on a key they ask the keyboard for the
 * key's code (func_ov095_0229c448: the character on it in the page shown, or a number from
 * 0x100 for the keys around the pages) and act on the code: a character is typed (the
 * keyboard puts Hangul together from the letters typed, as a phone does), 0x100 deletes
 * backwards. A character the text has no room for is refused with a flag set on the keyboard
 * (func_ov095_022999a8 with 0x40 or 0x80).
 *
 * So a character from another keyboard is typed by touching a key that every page has (the
 * space bar) and answering the question for its code with the character wanted: whatever
 * page is shown, and with everything the screen does about a typed character (the limits of
 * the text, the sounds) done by the game itself.
 *
 * The overlay's addresses are other overlays' at other times: the hooks are only taken for
 * the keyboard's own while the code there is the keyboard's (a number made of its bytes). */
#define KEYBOARD_TURN 0x0229bf7cu     /* func_ov095_0229bf7c(keyboard, ...): the keyboard's turn, each turn of the game */
#define KEYBOARD_KEY_CODE 0x0229c448u /* func_ov095_0229c448(keyboard, key, page): a key's code */
#define KEYBOARD_FLAG 0x022999a8u     /* func_ov095_022999a8(keyboard, bits): a flag set */
#define KEYBOARD_FULL 0xc0u           /* the flags for "no room" */
#define KEYBOARD_PAGE 0x1b            /* the keyboard's byte for the page shown */
/* the pages' tables of codes, by key: English small and capital, Hangul, symbols */
static const struct {
    uint32_t address;
    int count;
} tables[] = {{0x0229e874u, 53}, {0x0229e8e0u, 53}, {0x0229e774u, 34}, {0x0229e7b8u, 46}};
#define SYLLABLES 0x0229d0f8u /* the Hangul syllables the keyboard puts together: 0x933 of them */
#define SYLLABLE_COUNT 0x933

static int enabled;
static uint32_t ready_frame, keyboard; /* the frame the screen last waited in; the keyboard's object */
static int ready_known;
static uint32_t armed;                 /* the code the next key gives, or 0 */
static int taken, refused, foreign;
static unsigned long typed;

static uint16_t codes[256];
static int code_count;
static uint8_t syllable[11172 / 8 + 1];
static int tables_read;

static uint32_t bytes_number(uint32_t address, uint32_t size)
{
    const uint8_t *p = memory_host(address, size);
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; p != NULL && i < size; i++) {
        h = (h ^ p[i]) * 16777619u;
    }
    return p != NULL ? h : 0;
}

/* The numbers of the functions' first bytes in the Korean release. */
#define KEYBOARD_TURN_NUMBER 0xca8109f2u
#define KEYBOARD_KEY_CODE_NUMBER 0x88b9925du

/* The screens over the keyboard are overlays of their own, at the same addresses as one
 * another: which one is there is told by the code at a function each has (the one that acts
 * on a key's code: func_ov126_022a03dc of the name and text entry, func_ov111_0229f730 of
 * the chat), by the numbers of their first bytes in the Korean release. */
#define ENTRY_RUN_CODE 0x022a03dcu
#define ENTRY_RUN_CODE_NUMBER 0xb304c6bbu
#define CHAT_RUN_CODE 0x0229f730u
#define CHAT_RUN_CODE_NUMBER 0xc2c6303au

int game_keyboard_screen(void)
{
    if (!game_keyboard_ready()) {
        return KEYBOARD_SCREEN_NONE;
    }
    if (bytes_number(ENTRY_RUN_CODE, 24) == ENTRY_RUN_CODE_NUMBER) {
        return KEYBOARD_SCREEN_ENTRY;
    }
    if (bytes_number(CHAT_RUN_CODE, 24) == CHAT_RUN_CODE_NUMBER) {
        return KEYBOARD_SCREEN_CHAT;
    }
    return KEYBOARD_SCREEN_OTHER;
}

/* Whether the code at the hooked addresses is the keyboard's now (looked at once a frame:
 * other overlays' code at those addresses may come by often). */
static int is_keyboard(void)
{
    static uint32_t looked = 0xffffffffu;
    static int is;
    if (looked != io_frame()) {
        looked = io_frame();
        if (getenv("PORT_KEYBOARD_DUMP") != NULL) {
            fprintf(stderr, "keyboard: frame %u: numbers %08x %08x, screens %08x %08x\n", io_frame(), bytes_number(KEYBOARD_TURN, 24),
                    bytes_number(KEYBOARD_KEY_CODE, 24), bytes_number(ENTRY_RUN_CODE, 24), bytes_number(CHAT_RUN_CODE, 24));
        }
        is = bytes_number(KEYBOARD_TURN, 24) == KEYBOARD_TURN_NUMBER && bytes_number(KEYBOARD_KEY_CODE, 24) == KEYBOARD_KEY_CODE_NUMBER;
    }
    return is;
}

static uint16_t word16(uint32_t address)
{
    const uint8_t *p = memory_host(address, 2);
    return p != NULL ? (uint16_t) (p[0] | p[1] << 8) : 0;
}

/* The characters the keyboard's keys give and the syllables it knows, from the game's own
 * tables in memory (nothing of them is kept in the port). */
static void read_tables(void)
{
    code_count = 0;
    for (unsigned t = 0; t < sizeof(tables) / sizeof(tables[0]); t++) {
        for (int i = 0; i < tables[t].count && code_count < 256; i++) {
            uint16_t c = word16(tables[t].address + 2u * (uint32_t) i);
            int known = c == 0 || (c >= 0x100u && c < 0x124u); /* (no character: a key around the pages) */
            for (int k = 0; k < code_count && !known; k++) {
                known = codes[k] == c;
            }
            if (!known) {
                codes[code_count++] = c;
            }
        }
    }
    memset(syllable, 0, sizeof(syllable));
    for (int i = 0; i < SYLLABLE_COUNT; i++) {
        uint16_t s = word16(SYLLABLES + 2u * (uint32_t) i);
        if (s >= 0xac00u && s < 0xac00u + 11172u) {
            syllable[(s - 0xac00u) >> 3] |= (uint8_t) (1u << ((s - 0xac00u) & 7));
        }
    }
    tables_read = 1;
    if (getenv("PORT_KEYBOARD_DUMP") != NULL) {
        fprintf(stderr, "keyboard: %d characters:", code_count);
        for (int i = 0; i < code_count; i++) {
            fprintf(stderr, " %04x", codes[i]);
        }
        fprintf(stderr, "\n");
    }
}

static int turn_taken(ArmCpu *cpu, void *user)
{
    (void) user;
    if (!is_keyboard()) {
        return ARM_HOOK_QUIET;
    }
    keyboard = cpu->r[0];
    ready_frame = io_frame();
    ready_known = 1;
    if (!tables_read) {
        read_tables();
    }
    return ARM_HOOK_QUIET;
}

static int code_asked(ArmCpu *cpu, void *user)
{
    (void) user;
    if (!is_keyboard()) {
        return ARM_HOOK_QUIET;
    }
    if (armed == 0) {
        foreign = 1; /* a key of the keyboard's own was touched */
        return ARM_HOOK_QUIET;
    }
    /* the key's code is the character wanted: back to the caller with it */
    uint32_t lr = cpu->r[14];
    cpu->r[0] = armed;
    if (lr & 1) {
        cpu->cpsr |= ARM_CPSR_T;
    } else {
        cpu->cpsr &= ~ARM_CPSR_T;
    }
    cpu->r[15] = lr & ~1u;
    armed = 0;
    taken = 1;
    refused = 0;
    typed++;
    return ARM_HOOK_QUIET;
}

static int flag_set(ArmCpu *cpu, void *user)
{
    (void) user;
    if ((cpu->r[1] & KEYBOARD_FULL) && is_keyboard()) {
        refused = 1;
    }
    return ARM_HOOK_QUIET;
}

void game_keyboard_enable(int on)
{
    enabled = on != 0;
    if (!enabled) {
        armed = 0;
    }
    game_hooks_changed();
}

uint64_t game_keyboard_kinds(void)
{
    return enabled ? 1ull << GAME_HOOK_KIND(KEYBOARD_TURN) | 1ull << GAME_HOOK_KIND(KEYBOARD_KEY_CODE) |
                         1ull << GAME_HOOK_KIND(KEYBOARD_FLAG)
                   : 0;
}

ArmHook game_keyboard_hook_at(uint32_t address)
{
    if (!enabled) {
        return NULL;
    }
    return address == KEYBOARD_TURN ? turn_taken : address == KEYBOARD_KEY_CODE ? code_asked : address == KEYBOARD_FLAG ? flag_set : NULL;
}

int game_keyboard_ready(void)
{
    return enabled && ready_known && io_frame() - ready_frame <= 12;
}

void game_keyboard_type(uint32_t code)
{
    armed = code;
    taken = 0;
}

int game_keyboard_taken(void)
{
    int was = taken;
    taken = 0;
    return was;
}

int game_keyboard_refused(void)
{
    int was = refused;
    refused = 0;
    return was;
}

int game_keyboard_foreign(void)
{
    int was = foreign;
    foreign = 0;
    return was;
}

void game_keyboard_let_go(void)
{
    armed = 0;
}

int game_keyboard_gives(uint16_t code)
{
    for (int i = 0; i < code_count; i++) {
        if (codes[i] == code) {
            return 1;
        }
    }
    return 0;
}

int game_keyboard_syllable(uint16_t s)
{
    if (s < 0xac00u || s >= 0xac00u + 11172u) {
        return 0;
    }
    return !tables_read || (syllable[(s - 0xac00u) >> 3] >> ((s - 0xac00u) & 7) & 1);
}

unsigned long game_keyboard_typed(void)
{
    return typed;
}
