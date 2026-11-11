/* The port's own saving of the town (docs/autosave.md).
 *
 * The game keeps the whole town in one block of memory, the state block (decomp/clean/main/
 * state_block.h: data_021dc7a8, 0x173fc bytes), and its save is that block written to the
 * card as it is, twice, with sums (activity_card.c, ActivityCard_WriteStepSums). Nothing is
 * gathered before the write. So a save can be made without the game: the block copied out of
 * the DS's memory, given the same sums, and put where the game's two copies are in an image
 * of the card. The game's memory is only read, and the game runs as if nothing happened.
 *
 * When: at moments the game itself would let the player open its save menu, which is its own
 * word for "nothing is half done" (start_menu.c, StartMenu_UpdateIdle).
 *
 * All addresses are the Korean release's, by the decomp's names.
 */
#include "autosave.h"

#include <stdlib.h>
#include <string.h>

#include "memory.h"

/* the state block and its parts (state_block.h, card_save.c, activity_card.c) */
#define STATE_BLOCK 0x021dc7a8u
#define STATE_BLOCK_SIZE 0x173fcu
#define BLOCK_MARKER 0x0u         /* STATE_BLOCK_SET_UP once a town was set up */
#define BLOCK_SET_UP 0x32
#define BLOCK_PLAYERS 0x14u       /* the four town players' records */
#define BLOCK_EXCHANGE 0x11c70u   /* the exchange block: 0x950 bytes, its sum's halfword at 0x94a */
#define BLOCK_WAITING 0x1704cu    /* the waiting letter: 0x104 bytes, its sum's byte at 0x100 */
#define BLOCK_STAMP 0x173f8u      /* the stamp: the block's sum (halfword), a status, a serial */
#define STAMP_WRITTEN 2
/* a player's record (player_record.c) */
#define RECORD_SIZE 0x249cu
#define RECORD_ACCOUNT 0x1f0cu    /* the Wi-Fi account: 0x50 bytes, its sum's halfword at 0x4c */
#define RECORD_ROSTER 0x1f5cu     /* the friend roster: 0x484 bytes, its sum's halfword at 0x480 */
#define RECORD_FLAGS 0x23f8u
#define FLAG_UNSAVED 2            /* PLAYER_FLAG_UNSAVED: "this play has not been saved" */
#define TOWN_PLAYERS 4
/* where each copy of the state block is on the card (data_020d1c20: copy 0, copy 1, letters) */
#define CARD_OFFSETS 0x020d1c20u

/* what the START button's process looks at before it opens the save menu (start_menu.c) */
#define SCENE_CURRENT 0x020e54acu  /* SceneQuery_Current */
#define SCENE_45 45
#define EVENT_CURRENT 0x021c72b8u  /* the event under way, or NULL (event_queue.c) */
#define EVENT_FLAGS 0x021c72c0u    /* flags 1 and 8 keep the menu shut */
#define FIELD_READY 0x021c75b8u    /* 2 while the field takes input */
/* the session (session.h) and its players' records (session_players.h) */
#define SESSION 0x020ccf94u        /* a pointer */
#define SESSION_LOCAL_PLAYER 0x68u
#define SESSION_PLAYER_COUNT 0x6cu
#define SESSION_PLAYERS 0x021d5684u /* a pointer */
#define PLAYERS_RECORD_NUMBER 0x10u
/* the activity under way (activity_card.c): the game's own saving and loading, among others */
#define ACTIVITY_CURRENT 0x021f3c3cu
/* how this play started (start_kind.h): a new town or a new player cannot save yet, the menu
 * says, until the first day's errands are done */
#define START_KIND 0x021f3c30u
#define START_KIND_NEW_PLAYER 2
#define START_KIND_NEW_TOWN 1

static uint32_t word_at(uint32_t address)
{
    uint32_t value;
    memcpy(&value, memory_host(address, 4), 4);
    return value;
}

static uint8_t byte_at(uint32_t address)
{
    return *memory_host(address, 1);
}

/* An address the game holds, if it is one in main memory with `size` bytes after it. */
static int in_main_memory(uint32_t address, uint32_t size)
{
    return address >= 0x02000000u && address < 0x02400000u - size;
}

/* The number of this system's player's record, 0 to 3 for a player of the town; -1 else. */
static int own_record(void)
{
    uint32_t session = word_at(SESSION), players = word_at(SESSION_PLAYERS);
    if (!in_main_memory(session, 0x70) || !in_main_memory(players, 0x14)) {
        return -1;
    }
    uint32_t player = word_at(session + SESSION_LOCAL_PLAYER);
    if (player >= TOWN_PLAYERS || byte_at(session + SESSION_PLAYER_COUNT) >= 2) {
        return -1; /* no player yet, or other systems are in the session */
    }
    uint8_t number = byte_at(players + PLAYERS_RECORD_NUMBER + player);
    return number < TOWN_PLAYERS ? (int) number : -1;
}

int autosave_quiet(const char **why)
{
    const char *sink;
    why = why != NULL ? why : &sink;
    if (byte_at(STATE_BLOCK + BLOCK_MARKER) != BLOCK_SET_UP || byte_at(STATE_BLOCK + BLOCK_STAMP + 2) != STAMP_WRITTEN) {
        *why = "no town";
        return 0;
    }
    if (own_record() < 0) {
        *why = "no player of the town";
        return 0;
    }
    uint32_t kind = word_at(START_KIND);
    if (kind == START_KIND_NEW_PLAYER || kind == START_KIND_NEW_TOWN) {
        *why = "a new player's first day: the game does not save yet";
        return 0;
    }
    if (word_at(ACTIVITY_CURRENT) != 0) {
        *why = "an activity (the game's own saving)";
        return 0;
    }
    if (byte_at(SCENE_CURRENT) == SCENE_45 || byte_at(FIELD_READY) != 2) {
        *why = "the field takes no input";
        return 0;
    }
    if (word_at(EVENT_CURRENT) != 0 || (word_at(EVENT_FLAGS) & (1u | 8u)) != 0) {
        *why = "an event";
        return 0;
    }
    *why = "";
    return 1;
}

static uint16_t half_at(const uint8_t *p)
{
    return (uint16_t) (p[0] | p[1] << 8);
}

static void put_half(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t) value;
    p[1] = (uint8_t) (value >> 8);
}

/* The 16-bit sum of the halfwords of `length` bytes (BackupCard_Sum). */
static uint16_t sum_of(const uint8_t *data, uint32_t length)
{
    uint16_t sum = 0;
    for (uint32_t i = 0; i + 1 < length; i += 2) {
        sum = (uint16_t) (sum + half_at(data + i));
    }
    return sum;
}

/* The halfword at `at` set so that the part's sum is 0 (BackupCard_Checksum, as the game
 * calls it: the old value less the sum). */
static void settle(uint8_t *part, uint32_t length, uint32_t at)
{
    put_half(part + at, (uint16_t) (half_at(part + at) - sum_of(part, length)));
}

/* Whether the card holds a save of the game's own making: a copy of the state block with a
 * town and a right sum. The port's saves are that card with the town of the moment in it:
 * the third part, the letters, stays as the game left it (it is not in memory to be written,
 * and the game writes its copies without it too, when a town is loaded). */
static int card_saved(const uint8_t *card, const uint32_t offsets[2])
{
    int good = 0;
    for (int copy = 0; copy < 2; copy++) {
        const uint8_t *block = card + offsets[copy];
        good |= block[BLOCK_MARKER] == BLOCK_SET_UP && block[BLOCK_STAMP + 2] == STAMP_WRITTEN &&
                sum_of(block, STATE_BLOCK_SIZE) == 0;
    }
    return good;
}

int autosave_snapshot(uint8_t *card)
{
    int own = own_record();
    if (byte_at(STATE_BLOCK + BLOCK_MARKER) != BLOCK_SET_UP || own < 0) {
        return -1;
    }
    uint32_t offsets[2] = {word_at(CARD_OFFSETS), word_at(CARD_OFFSETS + 4)};
    if (offsets[0] > AUTOSAVE_CARD_SIZE - STATE_BLOCK_SIZE || offsets[1] > AUTOSAVE_CARD_SIZE - STATE_BLOCK_SIZE ||
        !card_saved(card, offsets)) {
        return -1;
    }
    uint8_t *block = malloc(STATE_BLOCK_SIZE);
    if (block == NULL) {
        return -1;
    }
    memcpy(block, memory_host(STATE_BLOCK, STATE_BLOCK_SIZE), STATE_BLOCK_SIZE);

    /* this play is saved: the flag the game sets when a town is loaded and clears when the
       player saves and stops, which otherwise brings Mr. Resetti at the next start */
    block[BLOCK_PLAYERS + (uint32_t) own * RECORD_SIZE + RECORD_FLAGS] &= (uint8_t) ~(1u << FLAG_UNSAVED);

    /* a serial the stamp did not have (SaveStamp_NewSerial); both copies get the same one,
       which is how the game tells a save that was written whole */
    uint8_t *stamp = block + BLOCK_STAMP;
    stamp[3] = (uint8_t) (stamp[3] + 1 + (uint8_t) (rand() % 254));

    /* the sums, in the game's order: the parts' own, then the block's in the stamp */
    settle(block + BLOCK_EXCHANGE, 0x950, 0x94a);
    uint8_t *waiting = block + BLOCK_WAITING;
    /* (this one is kept in a byte: the game stores the low 8 bits of what would bring the sum to 0) */
    waiting[0x100] = (uint8_t) (waiting[0x100] - sum_of(waiting, 0x104));
    for (uint32_t player = 0; player < TOWN_PLAYERS; player++) {
        uint8_t *record = block + BLOCK_PLAYERS + player * RECORD_SIZE;
        settle(record + RECORD_ACCOUNT, 0x50, 0x4c);
        settle(record + RECORD_ROSTER, 0x484, 0x480);
    }
    settle(block, STATE_BLOCK_SIZE, BLOCK_STAMP);

    memcpy(card + offsets[0], block, STATE_BLOCK_SIZE);
    memcpy(card + offsets[1], block, STATE_BLOCK_SIZE);
    free(block);
    return 0;
}
