#include "game_hooks.h"

#include <math.h>
#include <string.h>

#include "memory.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Words of the DS's memory. */
static int32_t load(uint32_t address)
{
    int32_t v = 0;
    const uint8_t *p = memory_host(address, 4);
    if (p != NULL) {
        memcpy(&v, p, 4);
    }
    return v;
}

static void store(uint32_t address, int32_t v)
{
    uint8_t *p = memory_host(address, 4);
    if (p != NULL) {
        memcpy(p, &v, 4);
        arm_memory_stored(address, 4);
    }
}

/* 20.12 fixed point, as the game multiplies (FX_MulFunc) */
static int32_t fx_mul(int32_t a, int32_t b)
{
    return (int32_t) (((int64_t) a * b + 0x800) >> 12);
}

/* Drawing above the lower screen -------------------------------------------------------------
 *
 * The game checks each of its objects (actors) against the camera's frustum once a frame,
 * when it asks whether the object may run (Actor_CanRun, decomp/clean/main/actor.c): outside,
 * or deeper than the object's own limit, the object is "far" (ACTOR_CULL_FAR), and a class
 * may skip running and drawing its far objects. Whether to draw is asked later
 * (Actor_CanDraw, func_020033ec): no, when far and the class skips drawing.
 *
 * The tall view shows what lies above the frustum's top plane, up to two screens higher. So
 * when Actor_CanDraw is entered for an object that is far, the check is made again here with
 * the top plane raised to there (from the object's place in view space, which the game's
 * check left in the object, and the frustum's other planes and distances as they are). If it
 * is then not far, the flag is cleared for the length of that one call and put back when it
 * returns. Only the answer to "draw?" changes: the flag is as the game set it everywhere else,
 * and whether the object runs, which is the game's simulation, is decided as before. */
#define ACTOR_CAN_DRAW 0x020033ecu     /* func_020033ec */
#define VIEW_FRUSTUM 0x021f6a6cu       /* data_021f6a6c: what the actors are culled against */
#define ACTOR_VIEW_POSITION 0x74
#define ACTOR_CULL_FLAGS 0xb0
#define ACTOR_CULL_RADIUS 0xb8
#define ACTOR_CULL_DEPTH 0xbc
#define ACTOR_CULL_SKIP_DRAW 2
#define ACTOR_CULL_FAR 4
enum { PLANE_LEFT, PLANE_TOP, PLANE_RIGHT, PLANE_BOTTOM };

static int draw_above;
static unsigned long drawn_above;

/* calls of Actor_CanDraw that have the flag cleared, to put back at their return */
static struct {
    uint32_t back, sp, actor;
} cleared[8];
static int cleared_count;

void game_hooks_draw_above(int on)
{
    draw_above = on;
}

unsigned long game_hooks_drawn_above(void)
{
    return drawn_above;
}

/* Whether the object at `view` (view space) with `radius` is inside the frustum whose top
 * plane is raised by two screens: the top edge of the picture is at y = h on the near plane,
 * the top of the tall view's upper part at y = 3h. */
static int near_with_raised_top(uint32_t actor)
{
    int32_t v[3], radius = load(actor + ACTOR_CULL_RADIUS);
    for (int k = 0; k < 3; k++) {
        v[k] = load(actor + ACTOR_VIEW_POSITION + 4 * (uint32_t) k);
    }
    int32_t depth = -v[2];
    if (radius == 0 || depth > load(actor + ACTOR_CULL_DEPTH) || depth < load(VIEW_FRUSTUM + 0x50) - radius ||
        depth > load(VIEW_FRUSTUM + 0x54) + radius) {
        return 0;
    }
    for (int plane = PLANE_LEFT; plane <= PLANE_BOTTOM; plane++) {
        int32_t n[3];
        if (plane == PLANE_TOP) {
            /* the game's top normal is (0, cos a, sin a) for half the field of view a; raised,
               the angle is the one whose tangent is three times as large */
            static int32_t for_fov = -1, raised[3];
            int32_t fov = load(VIEW_FRUSTUM + 0x58) & 0xffff;
            if (fov != for_fov) {
                double a = atan(3.0 * tan((fov >> 4) * (2.0 * M_PI / 4096.0)));
                raised[0] = 0;
                raised[1] = (int32_t) lrint(4096.0 * cos(a));
                raised[2] = (int32_t) lrint(4096.0 * sin(a));
                for_fov = fov;
            }
            memcpy(n, raised, sizeof(n));
        } else {
            for (int k = 0; k < 3; k++) {
                n[k] = load(VIEW_FRUSTUM + 4 + 12 * (uint32_t) plane + 4 * (uint32_t) k);
            }
        }
        if (fx_mul(v[2], n[2]) + (fx_mul(v[0], n[0]) + fx_mul(v[1], n[1])) > radius) {
            return 0;
        }
    }
    return 1;
}

static int can_draw_returned(ArmCpu *cpu, void *user)
{
    (void) user;
    uint32_t pc = cpu->r[15] & ~1u;
    for (int i = 0; i < cleared_count; i++) {
        if (cleared[i].back == pc && cleared[i].sp == cpu->r[13]) {
            store(cleared[i].actor + ACTOR_CULL_FLAGS, load(cleared[i].actor + ACTOR_CULL_FLAGS) | ACTOR_CULL_FAR);
            cleared[i] = cleared[--cleared_count];
            break;
        }
    }
    return ARM_HOOK_QUIET;
}

static int can_draw_entered(ArmCpu *cpu, void *user)
{
    (void) user;
    uint32_t actor = cpu->r[0];
    int32_t flags = load(actor + ACTOR_CULL_FLAGS);
    if ((flags & ACTOR_CULL_FAR) && (flags & ACTOR_CULL_SKIP_DRAW) && cleared_count < 8 && near_with_raised_top(actor)) {
        store(actor + ACTOR_CULL_FLAGS, flags & ~ACTOR_CULL_FAR);
        cleared[cleared_count].back = cpu->r[14] & ~1u;
        cleared[cleared_count].sp = cpu->r[13];
        cleared[cleared_count].actor = actor;
        cleared_count++;
        drawn_above++;
    }
    return ARM_HOOK_QUIET;
}

/* Mr. Resetti ---------------------------------------------------------------------------------
 *
 * Each player's record has a flag that is set while the game is played and cleared when it
 * is saved (PLAYER_FLAG_UNSAVED, decomp/clean/main/player_record.h). When a town is loaded
 * to be played (StateBlock_StartMode2, decomp/clean/main/state_block.c), the game asks for
 * that flag: still set, the last play ended without a save, so the count of resets is raised
 * and the mole is sent for. Then it sets the flag for this play.
 *
 * With the mole off, the answer to that one question is "no": the hook is at the address the
 * question's call returns to (0x0209ed3e, after `bl PlayerRecord_TestFlag` with flag 2 at
 * 0x0209ed3a in the Korean release), and clears r0. Nothing else reads differently, and the
 * flag itself goes on being set and cleared as the game does it. */
#define LOAD_UNSAVED_ANSWER 0x0209ed3eu

static int resetti;
static unsigned long resetti_kept_away;

void game_hooks_resetti(int on)
{
    resetti = on;
}

unsigned long game_hooks_resetti_kept_away(void)
{
    return resetti_kept_away;
}

static int unsaved_answered(ArmCpu *cpu, void *user)
{
    (void) user;
    if (cpu->r[0] != 0) {
        cpu->r[0] = 0;
        resetti_kept_away++;
    }
    return ARM_HOOK_QUIET;
}

ArmHook game_hook_at(uint32_t address, void **user)
{
    (void) user;
    if (address == LOAD_UNSAVED_ANSWER && !resetti) {
        return unsaved_answered;
    }
    if (!draw_above) {
        return NULL;
    }
    if (address == ACTOR_CAN_DRAW) {
        return can_draw_entered;
    }
    for (int i = 0; i < cleared_count; i++) {
        if (cleared[i].back == address) {
            return can_draw_returned;
        }
    }
    return NULL;
}
