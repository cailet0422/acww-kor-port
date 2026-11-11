#include "tall3d.h"

#include <pthread.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "gpu3d.h"
#include "io.h"
#include "render.h"

#include "scene.h"
#include "tall3d_engine.h"

/* the game's own engine (gpu3d_main.c) and the copies (gpu3d_tile.c, tall3d_engines.c) */
void gpu3d_main_init(void);
void gpu3d_main_write(uint32_t offset, uint32_t value);
void gpu3d_main_dot_depth_write(uint32_t value);
void gpu3d_main_gxstat_write(uint32_t value);
void gpu3d_main_vblank(void);
void gpu3d_main_render(int list);
const uint32_t *gpu3d_main_line_rgb6(int y);
extern const Engine3d *const tall3d_engines[TALL3D_ENGINES];

/* The picture ------------------------------------------------------------------------------- */

/* The picture the copies draw is `scale` times the DS's size and, with the tall view, two
 * screens high. It is cut into tiles of the DS's size, one copy of the engine each: `rows`
 * rows of `scale` tiles, the first row at the picture's top. At scale 1 the lower screen is
 * the game's own picture, so the only tile is the upper screen's. */
static int tall, scale = 1, rows, tiles, top_row;

typedef struct Tile {
    const Engine3d *engine;
    /* its part of the view: x' = scale * x + ox * w and y' = scale * y + oy * w, in clip
       coordinates (what lies in the tile is then what the engine sees as its whole screen) */
    int32_t ox, oy;
    /* the command whose parameters are arriving, as its thread plays the frames back */
    uint32_t cmd;
    int got, need, held, mode; /* mode: MTX_MODE, 0 is the projection */
    uint32_t params[32];
    /* the frames its thread has finished with: the geometry of, and the drawing of */
    uint32_t placed, drawn;
    int list; /* its list for the frame placed last, or -1 */
    pthread_t thread;
} Tile;
static Tile tile[TALL3D_ENGINES];

/* The scene engine, in place of the tiles: one copy of the engine that sees the whole view as
 * its screen and whose lists go to another renderer as scenes, undrawn here (scene.h). */
static SceneSink scene_sink;
static void *scene_user;

void tall3d_enable_scene(int tall_view, SceneSink sink, void *user)
{
    tall = tall_view != 0;
    scale = 1;
    top_row = 0;
    rows = tiles = 1;
    scene_sink = sink;
    scene_user = user;
    tile[0].engine = tall3d_engines[0];
    tile[0].ox = tile[0].oy = 0;
}

void tall3d_enable(int tall_view, int size)
{
    scene_sink = NULL;
    tall = tall_view != 0;
    scale = size < 1 ? 1 : size > TALL3D_SCALE_MAX ? TALL3D_SCALE_MAX : size;
    /* The whole view is two screens high, y from -w (the lower screen's bottom) to 3w (the
       upper screen's top), in 2 * scale rows of tiles; row j from the top has
       y' = scale * y + (2j + 1 - 3 * scale) * w. Without the tall view only the lower rows
       are drawn, and at scale 1 only the upper one. */
    top_row = tall ? 0 : scale;
    rows = scale == 1 ? (tall ? 1 : 0) : 2 * scale - top_row;
    tiles = rows * scale;
    for (int r = 0; r < rows; r++) {
        for (int i = 0; i < scale; i++) {
            Tile *t = &tile[r * scale + i];
            t->engine = tall3d_engines[r * scale + i];
            t->ox = scale - 2 * i - 1;
            t->oy = 2 * (top_row + r) + 1 - 3 * scale;
        }
    }
}

int tall3d_scale(void)
{
    return scale;
}

/* What a copy asks of the runtime ------------------------------------------------------------ */

/* Its list is drawn with the main engine's (gpu3d_render below): no request of its own. */
void tall3d_tile_render_3d(int list)
{
    (void) list;
}

/* The only register it would write is the overflow flag of DISP3DCNT, which is the game's to
 * see from the main engine alone. */
void tall3d_tile_reg_store(uint32_t offset, uint32_t value)
{
    (void) offset;
    (void) value;
}

/* The rear plane is not drawn (alpha 0): where the scene has nothing, what lies behind the 3D
 * layer shows. */
uint32_t tall3d_tile_view_reg(uint32_t offset)
{
    uint32_t v = io_view_reg(offset);
    return offset == 0x350 ? v & ~(31u << 16) : v;
}

char *tall3d_tile_getenv(const char *name)
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

/* A command for one tile's engine. */
static void send(Tile *t, uint32_t cmd, uint32_t param)
{
    t->engine->write(0x400 + cmd * 4, param);
}

/* The projection P as the game loads it becomes, for a tile, P x T: T takes a point to the
 * tile's part of the view (x' = scale * x + ox * w, y' = scale * y + oy * w). In the
 * row-vector form the engine uses, each row's x and y are scaled and take ox and oy of its w.
 * Everything else done to the projection (multiplications, the stack) is done to P x T as it
 * would be to P, so only the loads need this. */
static void load_moved(Tile *t, const int32_t m[16])
{
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            uint32_t v = (uint32_t) m[4 * r + c], w = (uint32_t) m[4 * r + 3];
            if (scene_sink != NULL) {
                /* the scene engine's screen is the whole view: as it is, or with the tall view
                   two screens high, y from -w to 3w brought to -w..w: y' = (y - w) / 2 */
                if (c == 1 && tall) {
                    v = (uint32_t) ((int32_t) (v - w) >> 1);
                }
            } else if (c == 0) {
                v = (uint32_t) scale * v + (uint32_t) t->ox * w;
            } else if (c == 1) {
                v = (uint32_t) scale * v + (uint32_t) t->oy * w;
            }
            send(t, 0x16, v);
        }
    }
}

/* Entries that are not the game's 3D commands but writes of two registers, in their order
 * among the commands. */
#define ENTRY_GXSTAT 0x80u
#define ENTRY_DOT_DEPTH 0x81u

/* One FIFO entry for a tile: a command's number with one of its parameters (or with nothing,
 * for a command without any). */
static void entry(Tile *t, uint32_t cmd, uint32_t value)
{
    if (cmd == ENTRY_GXSTAT) {
        t->engine->gxstat_write(value);
        return;
    }
    if (cmd == ENTRY_DOT_DEPTH) {
        t->engine->dot_depth_write(value);
        return;
    }
    cmd &= 0x7f;
    if (t->got == 0) {
        t->cmd = cmd;
        t->need = param_count[cmd];
        t->held = t->mode == 0 && (cmd == 0x16 || cmd == 0x17);
        if (t->need == 0) {
            if (cmd == 0x15 && t->mode == 0) { /* MTX_IDENTITY */
                static const int32_t identity[16] = {0x1000, 0, 0, 0, 0, 0x1000, 0, 0, 0, 0, 0x1000, 0, 0, 0, 0, 0x1000};
                load_moved(t, identity);
            } else {
                send(t, cmd, value);
            }
            return;
        }
    }
    t->params[t->got++] = value;
    if (!t->held) {
        send(t, t->cmd, value);
    }
    if (t->got < t->need) {
        return;
    }
    t->got = 0;
    if (t->cmd == 0x10) {
        t->mode = (int) (t->params[0] & 3);
    } else if (t->held) {
        int32_t m[16];
        if (t->cmd == 0x16) { /* MTX_LOAD_4x4 */
            memcpy(m, t->params, sizeof(m));
        } else { /* MTX_LOAD_4x3: the fourth column is (0, 0, 0, 1) */
            for (int r = 0; r < 4; r++) {
                for (int c = 0; c < 3; c++) {
                    m[4 * r + c] = (int32_t) t->params[3 * r + c];
                }
                m[4 * r + 3] = r == 3 ? 0x1000 : 0;
            }
        }
        load_moved(t, m);
    }
}

/* The frames, and the threads --------------------------------------------------------------
 *
 * A copy's work for a frame is the geometry (every command of the frame through its own
 * engine) and then the drawing of its tile. Done as the game writes the commands, that would
 * be the emulation's thread doing the geometry once for each tile. So the emulation's thread
 * only writes the frame's entries down. Each tile has a thread of its own that plays a frame
 * back into its engine when the frame is complete (at the swap), and draws its tile when the
 * drawing thread comes to that swap's 3D frame: the drawing thread tells the tiles to draw
 * and waits for them all, which is the moment the textures and registers it holds are the
 * ones this frame is drawn from. The tiles work side by side on as many cores as there are.
 *
 * A frame's entries are kept until every tile has drawn it; the emulation's thread waits at
 * a swap when FRAMES of them are still in use. */
#define FRAMES 4

typedef struct Frame {
    uint8_t *cmd;
    uint32_t *param;
    int count, room;
} Frame;

static Frame frames[FRAMES];
static uint32_t filling = 1;  /* the number of the frame being written down (numbers start at 1) */
static uint32_t complete;     /* the last frame complete: the tiles may play it back */
static uint32_t drawing;      /* the last frame the drawing thread asked the tiles to draw */
static uint32_t frame_of_list[GPU3D_LISTS]; /* the frame that goes with each of the main engine's lists */
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t moved = PTHREAD_COND_INITIALIZER;
static int threads_started;

/* packed command words taken apart (as gpu3d.c's fifo_write_packed does), on the
 * emulation's thread */
static struct {
    uint32_t packed;
    int packed_remaining, packed_params, packed_total;
} gx;

static void note(uint32_t cmd, uint32_t value)
{
    Frame *f = &frames[filling % FRAMES];
    if (f->count == f->room) {
        f->room = f->room ? f->room * 2 : 16384;
        f->cmd = realloc(f->cmd, (size_t) f->room);
        f->param = realloc(f->param, (size_t) f->room * sizeof(uint32_t));
    }
    f->cmd[f->count] = (uint8_t) cmd;
    f->param[f->count] = value;
    f->count++;
}

static int scene_given; /* (tall3d_lean, below) */

static void *tile_thread(void *arg)
{
    Tile *t = arg;
    for (uint32_t n = 1;; n++) {
        pthread_mutex_lock(&lock);
        while (complete < n) {
            pthread_cond_wait(&moved, &lock);
        }
        pthread_mutex_unlock(&lock);
        const Frame *f = &frames[n % FRAMES];
        for (int i = 0; i < f->count; i++) {
            entry(t, f->cmd[i], f->param[i]);
        }
        /* its list for this swap; then the swap, which lets the engine go on with the commands
           that waited for it and clears the list after this one */
        int list = t->engine->prepare();
        t->engine->vblank();
        pthread_mutex_lock(&lock);
        t->list = list;
        t->placed = n;
        pthread_cond_broadcast(&moved);
        while (drawing < n) {
            pthread_cond_wait(&moved, &lock);
        }
        pthread_mutex_unlock(&lock);
        if (list >= 0 && scene_sink != NULL) {
            static Scene scene;
            t->engine->scene(list, &scene);
            scene.tall = tall;
            scene_sink(&scene, scene_user);
            scene_given = 1;
        } else if (list >= 0) {
            t->engine->render(list);
        }
        pthread_mutex_lock(&lock);
        t->drawn = n;
        pthread_cond_broadcast(&moved);
        pthread_mutex_unlock(&lock);
    }
    return NULL;
}

static uint32_t all_drawn(void)
{
    uint32_t least = tile[0].drawn;
    for (int t = 1; t < tiles; t++) {
        least = tile[t].drawn < least ? tile[t].drawn : least;
    }
    return least;
}

static void feed(uint32_t offset, uint32_t value)
{
    if (offset >= 0x440) {
        if (offset < 0x600) {
            note((offset - 0x400) >> 2, value);
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
            note(cmd & 0x7f, value);
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

/* The engine's entry points (runtime/gpu3d.h), in front of all the engines ------------------- */

void gpu3d_init(void)
{
    gpu3d_main_init();
    if (tiles > 0 && !threads_started) {
        for (int t = 0; t < tiles; t++) {
            tile[t].engine->init();
            tile[t].list = -1;
            pthread_create(&tile[t].thread, NULL, tile_thread, &tile[t]);
        }
        memset(&gx, 0, sizeof(gx));
        threads_started = 1;
    }
}

void gpu3d_write(uint32_t offset, uint32_t value)
{
    gpu3d_main_write(offset, value);
    if (tiles > 0) {
        feed(offset, value);
    }
}

void gpu3d_dot_depth_write(uint32_t value)
{
    gpu3d_main_dot_depth_write(value);
    if (tiles > 0) {
        note(ENTRY_DOT_DEPTH, value);
    }
}

void gpu3d_gxstat_write(uint32_t value)
{
    gpu3d_main_gxstat_write(value);
    if (tiles > 0) {
        note(ENTRY_GXSTAT, value);
    }
}

/* A swap. The main engine, at VBlank with a swap pending, asks for its list's drawing
 * (render_3d) and starts on the next frame. Its request comes here first: the frame written
 * down is complete, which the tiles' threads are told, and goes with the main engine's list
 * when the drawing thread comes to it. */
void tall3d_main_render_3d(int list)
{
    if (tiles > 0) {
        pthread_mutex_lock(&lock);
        frame_of_list[list] = filling;
        complete = filling;
        pthread_cond_broadcast(&moved);
        filling++;
        /* the next frame is written where frame `filling - FRAMES` was: when all have drawn it */
        while (filling > FRAMES && all_drawn() + FRAMES < filling) {
            pthread_cond_wait(&moved, &lock);
        }
        frames[filling % FRAMES].count = 0;
        pthread_mutex_unlock(&lock);
    }
    render_3d(list);
}

void gpu3d_vblank(void)
{
    gpu3d_main_vblank();
}

/* tall3d_lean: the main engine's own picture is left undrawn while nothing shows it. That is
 * when the scene goes to a GPU and the lower screen shows the 3D layer (then the GPU's
 * picture is what is seen: tall3d_scene_state), and the game is not capturing the display
 * (which reads the engine's lines). A capture is rare and announced by its register; the
 * engine's picture is drawn for a second after one was seen, to be there for the next. */
static int lean;
static unsigned lean_hold;
static unsigned long lean_left, lean_drawn;
static int main_list = -1;  /* the main engine's list left undrawn, or -1: its picture is the newest */
static int frame_unseen;    /* the frame being put together is one whose main picture nobody sees */
static int scene_given;     /* a scene has been handed out: there is something to see in its place */

void tall3d_lean(int on)
{
    lean = on != 0;
}

void tall3d_lean_counts(unsigned long *left, unsigned long *drawn)
{
    *left = lean_left;
    *drawn = lean_drawn;
}

static int main_picture_needed(void)
{
    if (!lean || scene_sink == NULL || !scene_given) {
        return 1;
    }
    uint32_t dispcnt = io_view_reg(0x000);
    int shown = !(io_view_reg(0x304) & 0x8000) && ((dispcnt >> 16) & 3) == 1 && !(dispcnt & 0x80) && (dispcnt & 0x008) &&
                (dispcnt & 0x100);
    return !shown || lean_hold > 0 || (io_view_reg(0x064) & 0x80000000u) != 0;
}

/* On the drawing thread, before the first line of a frame (gpu2d_main.c): whether this
 * frame's main picture is one nobody sees. If it is seen after all and the engine's picture
 * was left undrawn at the swap (what the registers said then is not what they say now), it
 * is drawn now. */
int tall3d_frame_unseen(void)
{
    if (io_view_reg(0x064) & 0x80000000u) {
        lean_hold = 60;
    } else if (lean_hold > 0) {
        lean_hold--;
    }
    frame_unseen = !main_picture_needed();
    if (!frame_unseen && main_list >= 0) {
        gpu3d_main_render(main_list);
        main_list = -1;
        lean_drawn++;
    }
    return frame_unseen;
}

/* On the drawing thread (or at once, without one): the main engine's list, and the tiles of
 * the frame that goes with it, drawn by their threads while this one waits. */
void gpu3d_render(int list)
{
    if (main_picture_needed()) {
        gpu3d_main_render(list);
        main_list = -1;
        lean_drawn++;
    } else {
        main_list = list;
        lean_left++;
    }
    if (tiles > 0 && list >= 0 && list < GPU3D_LISTS) {
        pthread_mutex_lock(&lock);
        uint32_t n = frame_of_list[list];
        if (n > drawing) {
            drawing = n;
            pthread_cond_broadcast(&moved);
        }
        while (all_drawn() < n) {
            pthread_cond_wait(&moved, &lock);
        }
        pthread_mutex_unlock(&lock);
    }
}

/* The picture's lines ------------------------------------------------------------------------ */

/* Taken on the drawing thread whenever the 2D engine takes the 3D picture's lines, which it
 * does when the picture has changed: what is kept here always goes with the picture the
 * lower screen is showing. Rows of TALL3D_W * scale pixels, the picture's top row first. */
static uint32_t picture[2 * TALL3D_H * TALL3D_SCALE_MAX][TALL3D_W * TALL3D_SCALE_MAX];
static int picture_valid;

const uint32_t *gpu3d_line_rgb6(int y)
{
    if (tiles > 0 && scene_sink == NULL && y >= 0 && y < TALL3D_H) {
        /* the DS's line y is `scale` rows of each screen's part of the picture */
        for (int half = top_row / scale; half < 2; half++) {
            for (int k = 0; k < scale; k++) {
                int row = half * TALL3D_H * scale + y * scale + k; /* in the whole two-screen view */
                int r = row / TALL3D_H - top_row;                  /* its row of tiles */
                if (r < 0 || r >= rows) {
                    continue;
                }
                for (int i = 0; i < scale; i++) {
                    memcpy(&picture[row][i * TALL3D_W], tile[r * scale + i].engine->line_rgb6(row % TALL3D_H),
                           TALL3D_W * sizeof(uint32_t));
                }
            }
        }
        picture_valid = 1;
    }
    return gpu3d_main_line_rgb6(y);
}

int tall3d_scene_state(int *over_upper, uint32_t *brightness)
{
    /* as tall3d_picture below: whether the lower screen shows the 3D layer at all, and
       whether a window hides it outside (then the upper part is left out) */
    uint32_t dispcnt = io_view_reg(0x000);
    int shown = !(io_view_reg(0x304) & 0x8000) && ((dispcnt >> 16) & 3) == 1 && !(dispcnt & 0x80) && (dispcnt & 0x008) &&
                (dispcnt & 0x100);
    uint32_t windows = io_view_reg(0x048);
    int windowed = (dispcnt & 0xe000) && !((windows >> 16) & 1);
    *over_upper = tall && !windowed;
    *brightness = io_view_reg(0x06c);
    /* (a frame put together as one whose main picture nobody sees has only the scene to show) */
    return (shown || frame_unseen) && scene_sink != NULL;
}

int tall3d_picture(const uint32_t **upper, const uint32_t **lower, int *stride, uint32_t *brightness)
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
    int windowed = (dispcnt & 0xe000) && !((windows >> 16) & 1);
    if (!shown) {
        picture_valid = 0;
    }
    if (tiles == 0 || !picture_valid || scene_sink != NULL) {
        return 0;
    }
    *stride = TALL3D_W * TALL3D_SCALE_MAX;
    *upper = tall && !windowed ? &picture[0][0] : NULL;
    *lower = scale > 1 ? &picture[TALL3D_H * scale][0] : NULL;
    *brightness = io_view_reg(0x06c); /* MASTER_BRIGHT of the main engine */
    return 1;
}
