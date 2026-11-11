/* The DS's 2D engines: decomp/runtime/gpu2d.c as it is, with the entry points the mix of a
 * larger 3D picture needs to stand in front of given other names (mix2d.c has the functions
 * of the original names, which call these). Nothing of the engines changes.
 *
 * Added behind them, for that mix (mix2d.h): the main engine's line composed three more times
 * with a 3D layer of one flat colour (black, white, nothing) in place of the game's. Only
 * the composing is done again: the line's backgrounds, objects and windows are the ones just
 * drawn for the game's own picture, which do not depend on what the 3D layer holds. */
#define gpu2d_init gpu2d_main_init
#define gpu2d_line gpu2d_main_line
#define gpu2d_register_written gpu2d_main_register_written

#include "gpu2d.c"

static uint32_t probe_frame[3][GPU2D_HEIGHT * GPU2D_WIDTH];

/* tall3d.c: for the frame whose first line is about to be drawn, whether the main engine's own
 * picture is one nobody sees (the scene's goes to a GPU, which mixes it by the answers). */
int tall3d_frame_unseen(void);

/* What a line's answers are made from is the same from frame to frame much of the time (the
 * game's 2D layers over its 3D picture stand still, or are not there): a number made of all
 * of it is kept for each line, and while the number stays, so do the answers. */
static uint64_t probe_made_of[GPU2D_HEIGHT];
static uint8_t probe_kept[GPU2D_HEIGHT];
static unsigned long probe_lines, probe_lines_kept, probe_wrong;
static int probe_check = -1; /* PORT_MIX_CHECK: kept answers are made again and compared */

static inline uint64_t mixed(uint64_t h, uint64_t v)
{
    h = (h ^ v) * 0x9e3779b97f4a7c15ull;
    return h ^ (h >> 32);
}

static uint64_t mixed_bytes(uint64_t h, const void *data, size_t size)
{
    const uint8_t *p = data;
    for (size_t i = 0; i + 8 <= size; i += 8) {
        uint64_t v;
        memcpy(&v, p + i, 8);
        h = mixed(h, v);
    }
    return h;
}

/* Whether the main engine's line has the 3D layer in it. */
static int line_has_3d(const Engine *e, uint32_t dispcnt)
{
    return ((dispcnt >> 16) & 3) == 1 && !(dispcnt & 0x80) && (dispcnt & 0x100) && bg_kind(e, 0, dispcnt) == BG_3D;
}

/* Everything compose and the line's end read for a line with a flat 3D layer, as a number. */
static uint64_t line_made_of(const Engine *e, uint32_t dispcnt)
{
    uint64_t h = dispcnt;
    h = mixed(h, reg16(e, R_BLDCNT) | (uint64_t) reg16(e, R_BLDALPHA) << 16 | (uint64_t) reg16(e, R_BLDY) << 32 |
                     (uint64_t) reg16(e, R_MOSAIC) << 48);
    h = mixed(h, reg16(e, R_BG0CNT) | (uint64_t) reg16(e, R_BG0CNT + 2) << 16 | (uint64_t) reg16(e, R_BG0CNT + 4) << 32 |
                     (uint64_t) reg16(e, R_BG0CNT + 6) << 48);
    h = mixed(h, reg16(e, R_BG0HOFS) | (uint64_t) reg16(e, R_MASTER_BRIGHT) << 16 | (uint64_t) pal_colour(bg_palette(e), 0) << 32);
    h = mixed(h, e->obj_prios | (uint64_t) e->obj_kinds << 32);
    for (int bg = 1; bg < 4; bg++) {
        if ((dispcnt & (0x100u << bg)) != 0 && bg_kind(e, bg, dispcnt) != BG_NONE) {
            h = mixed_bytes(h, e->bg[bg], sizeof(e->bg[bg]));
        }
    }
    if ((dispcnt & 0x1000) != 0 && e->obj_prios != 0) {
        h = mixed_bytes(h, e->obj, sizeof(e->obj));
        h = mixed_bytes(h, e->obj_prio, sizeof(e->obj_prio));
        h = mixed_bytes(h, e->obj_flags, sizeof(e->obj_flags));
        h = mixed_bytes(h, e->obj_alpha, sizeof(e->obj_alpha));
    }
    return mixed_bytes(h, e->win, sizeof(e->win));
}

/* The three answers for line y of the main engine, into `to` (three rows): the line composed
 * with each flat layer, as draw_bgs and draw_3d place the layer and finish_line ends the line. */
static void probe_compose(Engine *e, uint32_t dispcnt, uint32_t *to[3])
{
    static const uint32_t colour[3] = {0, 0x3ffffu, NONE};
    static const uint8_t alpha[3] = {31, 31, 0};
    uint32_t cnt = reg16(e, R_BG0CNT), mosaic = reg16(e, R_MOSAIC), hofs = reg16(e, R_BG0HOFS) & 0x1ffu;
    uint32_t mh = (cnt & 0x40) ? mosaic & 15 : 0;
    uint32_t mb = reg16(e, R_MASTER_BRIGHT), factor = mb & 31, mode = mb >> 14;
    uint8_t table[64];
    factor = factor > 16 ? 16 : factor;
    int faded = (mode == 1 || mode == 2) && factor > 0;
    if (faded) {
        fade_table(table, factor, mode == 1, mode == 1 ? 0 : 15);
    }
    for (int k = 0; k < 3; k++) {
        for (uint32_t x = 0; x < GPU2D_WIDTH; x++) {
            uint32_t sx = (x + hofs) & 0x1ffu;
            e->bg[0][x] = sx < 256 ? colour[k] : NONE;
            e->alpha3d[x] = sx < 256 ? alpha[k] : 0;
        }
        mosaic_line(e->bg[0], mh);
        compose(e, dispcnt);
        if (faded) {
            for (uint32_t x = 0; x < GPU2D_WIDTH; x++) {
                to[k][x] = fade(table, e->out[x]);
            }
        } else {
            memcpy(to[k], e->out, sizeof(e->out));
        }
    }
}

/* The answers for line y of the main engine, whose line was just drawn (and, unless nobody
 * sees it, composed and finished: `row_made`). */
static void probe_line(Engine *e, uint32_t y, uint32_t dispcnt)
{
    uint32_t *rows[3] = {probe_frame[0] + y * GPU2D_WIDTH, probe_frame[1] + y * GPU2D_WIDTH, probe_frame[2] + y * GPU2D_WIDTH};
    if (!line_has_3d(e, dispcnt)) {
        /* no 3D layer in the line (or not the engine's graphics at all): whatever its colour */
        const uint32_t *row = e->frame + y * GPU2D_WIDTH;
        for (int k = 0; k < 3; k++) {
            memcpy(rows[k], row, GPU2D_WIDTH * sizeof(uint32_t));
        }
        probe_kept[y] = 0;
        return;
    }
    if (probe_check < 0) {
        probe_check = getenv("PORT_MIX_CHECK") != NULL;
    }
    uint64_t made_of = line_made_of(e, dispcnt);
    probe_lines++;
    if (probe_kept[y] && probe_made_of[y] == made_of) {
        probe_lines_kept++;
        if (probe_check) {
            static uint32_t again[3][GPU2D_WIDTH];
            uint32_t *to[3] = {again[0], again[1], again[2]};
            probe_compose(e, dispcnt, to);
            for (int k = 0; k < 3; k++) {
                probe_wrong += memcmp(again[k], rows[k], sizeof(again[k])) != 0;
            }
        }
        return;
    }
    probe_compose(e, dispcnt, rows);
    probe_made_of[y] = made_of;
    probe_kept[y] = 1;
}

/* gpu2d_line, and the main engine's three answers after its line. In a frame whose main
 * picture nobody sees, the main engine's lines with the 3D layer are not composed (their
 * rows stay as they were): the answers are all that is taken of them. A display capture
 * reads the composed line, so not while one runs or is asked for. */
void gpu2d_main_line_probed(uint32_t y)
{
    static int unseen;
    if (y >= GPU2D_HEIGHT) {
        return;
    }
    if (y == 0) {
        unseen = tall3d_frame_unseen();
    }
    for (int i = 0; i < 2; i++) {
        Engine *e = &engines[i];
        uint32_t dispcnt = reg32(e, R_DISPCNT);
        if (y == 0) {
            e->ref_reload[0] = e->ref_reload[1] = 1;
        }
        for (int k = 0; k < 2; k++) {
            if (e->ref_reload[k]) {
                e->ref_x[k] = (int32_t) (reg32(e, 0x28 + 0x10 * (uint32_t) k) << 4) >> 4;
                e->ref_y[k] = (int32_t) (reg32(e, 0x2c + 0x10 * (uint32_t) k) << 4) >> 4;
                e->ref_reload[k] = 0;
            }
        }
        draw_bgs(e, y, dispcnt);
        draw_objs(e, y, dispcnt);
        build_window(e, y, dispcnt);
        int left = i == 0 && unseen && !e->capturing && !(reg32(e, R_DISPCAPCNT) >> 31) && line_has_3d(e, dispcnt);
        if (!left) {
            compose(e, dispcnt);
            if (i == 0) {
                capture_line(e, y, dispcnt, (dispcnt & 8) ? gpu3d_line((int) y) : NULL);
            }
            finish_line(e, y, dispcnt);
        }
        if (i == 0) {
            probe_line(e, y, dispcnt);
        }
    }
}

/* An answer's lines: 256 x 192 pixels of red | green << 6 | blue << 12. */
const uint32_t *gpu2d_main_probe_frame(int probe)
{
    return probe_frame[probe];
}

/* For the log: the lines answers were asked for, how many were kept from the frame before,
 * and (PORT_MIX_CHECK) how many of the kept were not what making them again gave. */
void gpu2d_main_probe_counts(unsigned long *lines, unsigned long *kept, unsigned long *wrong)
{
    *lines = probe_lines;
    *kept = probe_lines_kept;
    *wrong = probe_wrong;
}
