#include "tall3d.h"

#include <stddef.h>
#include <string.h>

#include "gpu3d.h"
#include "io.h"
#include "render.h"

/* the two engines (gpu3d_main.c, gpu3d_upper.c) */
void gpu3d_main_init(void);
void gpu3d_main_write(uint32_t offset, uint32_t value);
void gpu3d_main_dot_depth_write(uint32_t value);
void gpu3d_main_gxstat_write(uint32_t value);
void gpu3d_main_vblank(void);
void gpu3d_main_render(int list);
const uint32_t *gpu3d_main_line_rgb6(int y);

void gpu3d_upper_init(void);
void gpu3d_upper_write(uint32_t offset, uint32_t value);
void gpu3d_upper_dot_depth_write(uint32_t value);
void gpu3d_upper_gxstat_write(uint32_t value);
void gpu3d_upper_vblank(void);
void gpu3d_upper_render(int list);
const uint32_t *gpu3d_upper_line_rgb6(int y);

static int enabled;

void tall3d_enable(int on)
{
    enabled = on;
}

/* What the second engine asks of the runtime ------------------------------------------------- */

/* A swap. The main engine, at VBlank with a swap pending, asks for its list's drawing
 * (render_3d), then starts filling the next list. render_3d waits there until the drawing
 * thread is done with that next list, and only then queues the request. The second engine
 * has to swap inside that very step: its own list must be complete before the request is
 * queued (the thread draws both lists for it), and its next list may be cleared only after
 * the wait. So the main engine's request comes here, the second engine swaps, and its
 * request, made at the same point of its swap, is the one that goes to render_3d. */
static int upper_list_of[GPU3D_LISTS]; /* the second engine's list drawn with each main one, or -1 */
static int swap_list, swap_asked;

void tall3d_upper_render_3d(int list)
{
    upper_list_of[swap_list] = list;
    swap_asked = 1;
    render_3d(swap_list);
}

void tall3d_main_render_3d(int list)
{
    if (!enabled) {
        render_3d(list);
        return;
    }
    swap_list = list;
    swap_asked = 0;
    upper_list_of[list] = -1;
    gpu3d_upper_vblank();
    if (!swap_asked) { /* the second engine had no swap pending: the main picture alone */
        render_3d(list);
    }
}

/* The only register it would write is the overflow flag of DISP3DCNT, which is the game's to
 * see from the main engine alone. */
void tall3d_upper_reg_store(uint32_t offset, uint32_t value)
{
    (void) offset;
    (void) value;
}

/* The rear plane is not drawn (alpha 0): where the scene has nothing, the upper screen's own
 * picture shows. */
uint32_t tall3d_upper_view_reg(uint32_t offset)
{
    uint32_t v = io_view_reg(offset);
    return offset == 0x350 ? v & ~(31u << 16) : v;
}

char *tall3d_upper_getenv(const char *name)
{
    (void) name;
    return NULL;
}

/* The commands, with the projection moved --------------------------------------------------- */

/* Parameters per command (GBATEK, "DS 3D Geometry Commands"). */
static const uint8_t param_count[0x80] = {
    [0x10] = 1, [0x11] = 0, [0x12] = 1, [0x13] = 1, [0x14] = 1, [0x15] = 0, [0x16] = 16, [0x17] = 12, [0x18] = 16, [0x19] = 12,
    [0x1a] = 9, [0x1b] = 3, [0x1c] = 3, [0x20] = 1, [0x21] = 1, [0x22] = 1, [0x23] = 2,  [0x24] = 1,  [0x25] = 1,  [0x26] = 1,
    [0x27] = 1, [0x28] = 1, [0x29] = 1, [0x2a] = 1, [0x2b] = 1, [0x30] = 1, [0x31] = 1,  [0x32] = 1,  [0x33] = 1,  [0x34] = 32,
    [0x40] = 1, [0x41] = 0, [0x50] = 1, [0x60] = 1, [0x70] = 3, [0x71] = 2, [0x72] = 1,
};

static int command_valid(uint32_t cmd)
{
    return (cmd >= 0x10 && cmd <= 0x1c) || (cmd >= 0x20 && cmd <= 0x2b) || (cmd >= 0x30 && cmd <= 0x34) || cmd == 0x40 ||
           cmd == 0x41 || cmd == 0x50 || cmd == 0x60 || (cmd >= 0x70 && cmd <= 0x72);
}

static struct {
    /* a packed command word being taken apart (as gpu3d.c's fifo_write_packed does) */
    uint32_t packed;
    int packed_remaining, packed_params, packed_total;
    /* the command whose parameters are arriving */
    uint32_t cmd;
    int got, need, held;
    uint32_t params[32];
    int mode; /* MTX_MODE: 0 is the projection */
} gx;

static void send(uint32_t cmd, uint32_t param)
{
    gpu3d_upper_write(0x400 + cmd * 4, param);
}

/* The projection P as the game loads it becomes P x T, where T moves a point up by one screen
 * (y' = y - 2w): in the row-vector form the engine uses, each row's y takes -2 of its w.
 * Everything else done to the projection (multiplications, the stack) is done to P x T as it
 * would be to P, so only the loads need this. */
static void load_moved(const int32_t m[16])
{
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            int32_t v = m[4 * r + c];
            if (c == 1) {
                v = (int32_t) ((uint32_t) v - 2u * (uint32_t) m[4 * r + 3]);
            }
            send(0x16, (uint32_t) v);
        }
    }
}

/* One FIFO entry: a command's number with one of its parameters (or with nothing, for a
 * command without any). */
static void entry(uint32_t cmd, uint32_t value)
{
    cmd &= 0x7f;
    if (gx.got == 0) {
        gx.cmd = cmd;
        gx.need = param_count[cmd];
        gx.held = gx.mode == 0 && (cmd == 0x16 || cmd == 0x17);
        if (gx.need == 0) {
            if (cmd == 0x15 && gx.mode == 0) { /* MTX_IDENTITY */
                static const int32_t identity[16] = {0x1000, 0, 0, 0, 0, 0x1000, 0, 0, 0, 0, 0x1000, 0, 0, 0, 0, 0x1000};
                load_moved(identity);
            } else {
                send(cmd, value);
            }
            return;
        }
    }
    gx.params[gx.got++] = value;
    if (!gx.held) {
        send(gx.cmd, value);
    }
    if (gx.got < gx.need) {
        return;
    }
    gx.got = 0;
    if (gx.cmd == 0x10) {
        gx.mode = (int) (gx.params[0] & 3);
    } else if (gx.held) {
        int32_t m[16];
        if (gx.cmd == 0x16) { /* MTX_LOAD_4x4 */
            memcpy(m, gx.params, sizeof(m));
        } else { /* MTX_LOAD_4x3: the fourth column is (0, 0, 0, 1) */
            for (int r = 0; r < 4; r++) {
                for (int c = 0; c < 3; c++) {
                    m[4 * r + c] = (int32_t) gx.params[3 * r + c];
                }
                m[4 * r + 3] = r == 3 ? 0x1000 : 0;
            }
        }
        load_moved(m);
    }
}

static void feed(uint32_t offset, uint32_t value)
{
    if (offset >= 0x440) {
        if (offset < 0x600) {
            entry((offset - 0x400) >> 2, value);
        }
        return;
    }
    /* a packed word, or the next parameter of one */
    if (gx.packed_remaining == 0) {
        gx.packed = value;
        gx.packed_remaining = 4;
        gx.packed_params = 0;
        gx.packed_total = param_count[gx.packed & 0x7f];
        if (gx.packed_total > 0 && command_valid(gx.packed & 0xff)) {
            return; /* its parameters follow */
        }
    } else {
        gx.packed_params++;
    }
    for (;;) {
        uint32_t cmd = gx.packed & 0xff;
        if (cmd != 0 || gx.packed_remaining == 4) {
            entry(cmd, value);
        }
        if (gx.packed_params >= gx.packed_total) {
            gx.packed >>= 8;
            gx.packed_remaining--;
            if (gx.packed_remaining == 0) {
                return;
            }
            gx.packed_params = 0;
            gx.packed_total = param_count[gx.packed & 0x7f];
            if (!command_valid(gx.packed & 0xff)) {
                gx.packed_total = 0;
            }
        }
        if (gx.packed_params < gx.packed_total) {
            return;
        }
    }
}

/* The engine's entry points (runtime/gpu3d.h), in front of both engines ----------------------- */

void gpu3d_init(void)
{
    gpu3d_main_init();
    if (enabled) {
        gpu3d_upper_init();
        memset(&gx, 0, sizeof(gx));
    }
}

void gpu3d_write(uint32_t offset, uint32_t value)
{
    gpu3d_main_write(offset, value);
    if (enabled) {
        feed(offset, value);
    }
}

void gpu3d_dot_depth_write(uint32_t value)
{
    gpu3d_main_dot_depth_write(value);
    if (enabled) {
        gpu3d_upper_dot_depth_write(value);
    }
}

void gpu3d_gxstat_write(uint32_t value)
{
    gpu3d_main_gxstat_write(value);
    if (enabled) {
        gpu3d_upper_gxstat_write(value);
    }
}

void gpu3d_vblank(void)
{
    gpu3d_main_vblank(); /* the second engine swaps within it (tall3d_main_render_3d) */
}

void gpu3d_render(int list)
{
    gpu3d_main_render(list);
    if (enabled && list >= 0 && list < GPU3D_LISTS && upper_list_of[list] >= 0) {
        gpu3d_upper_render(upper_list_of[list]);
    }
}

/* The frame's lines -------------------------------------------------------------------------- */

/* Taken on the drawing thread whenever the 2D engine takes the 3D picture's lines, which it
 * does when the picture has changed: the upper part kept here is always the one that goes
 * with the picture the lower screen is showing. */
static uint32_t upper_lines[TALL3D_H][TALL3D_W];
static int upper_valid;

const uint32_t *gpu3d_line_rgb6(int y)
{
    if (enabled && y >= 0 && y < TALL3D_H) {
        memcpy(upper_lines[y], gpu3d_upper_line_rgb6(y), sizeof(upper_lines[y]));
        upper_valid = 1;
    }
    return gpu3d_main_line_rgb6(y);
}

int tall3d_frame(uint32_t pixels[TALL3D_H][TALL3D_W])
{
    /* The lower screen shows the 3D picture when the main engine is the lower screen's
       (POWCNT1 bit 15 clear), displays normally (DISPCNT mode 1, not blanked) and has BG0 on
       as the 3D layer. Otherwise there is nothing to continue, and what was kept is old. */
    uint32_t dispcnt = io_view_reg(0x000);
    int shown = !(io_view_reg(0x304) & 0x8000) && ((dispcnt >> 16) & 3) == 1 && !(dispcnt & 0x80) && (dispcnt & 0x008) &&
                (dispcnt & 0x100);
    /* Windows (the round wipe between scenes): where the game hides the 3D layer outside
       its windows, the upper part, which has no window of its own, is left out whole. */
    uint32_t windows = io_view_reg(0x048); /* WININ, and WINOUT in the upper half */
    if (shown && (dispcnt & 0xe000) && !((windows >> 16) & 1)) {
        return 0;
    }
    if (!shown) {
        upper_valid = 0;
    }
    if (!enabled || !upper_valid) {
        return 0;
    }
    /* the main engine's master brightness: mode 1 towards white, 2 towards black, by factor/16 */
    uint32_t bright = io_view_reg(0x06c), mode = (bright >> 14) & 3, factor = bright & 31;
    if (factor > 16) {
        factor = 16;
    }
    for (int y = 0; y < TALL3D_H; y++) {
        for (int x = 0; x < TALL3D_W; x++) {
            uint32_t p = upper_lines[y][x];
            if ((p >> 24) != 0 && (mode == 1 || mode == 2) && factor != 0) {
                uint32_t out = p & 0xff000000u;
                for (int s = 0; s < 24; s += 8) {
                    uint32_t c = (p >> s) & 63;
                    c = mode == 1 ? c + (63 - c) * factor / 16 : c - c * factor / 16;
                    out |= c << s;
                }
                p = out;
            }
            pixels[y][x] = p;
        }
    }
    return 1;
}
