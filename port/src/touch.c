#include "touch.h"

#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* The player's arrangement ------------------------------------------------------------------- */

/* Each item's place and size as the player left them, relative to where the layout puts it:
 * moved by (dx, dy) of the window's shorter side, and `scale` times as large. Tall and wide
 * windows are arranged apart ([0] tall, [1] wide). */
typedef struct Custom {
    float dx, dy, scale;
} Custom;

static Custom custom[2][ITEM_COUNT];
static int custom_ready;
static const char *const item_names[ITEM_COUNT] = {"pad", "face", "l", "r", "select", "start"};

static void custom_init(void)
{
    if (!custom_ready) {
        for (int o = 0; o < 2; o++) {
            for (int i = 0; i < ITEM_COUNT; i++) {
                custom[o][i] = (Custom){0, 0, 1};
            }
        }
        custom_ready = 1;
    }
}

int touch_settings_line(const char *line)
{
    char orient[8], name[16];
    float dx, dy, scale;
    custom_init();
    if (sscanf(line, "button.%7[a-z].%15[a-z]=%f,%f,%f", orient, name, &dx, &dy, &scale) != 5) {
        return 0;
    }
    int o = strcmp(orient, "wide") == 0;
    for (int i = 0; i < ITEM_COUNT; i++) {
        if (strcmp(name, item_names[i]) == 0 && scale >= 0.3f && scale <= 4.0f) {
            custom[o][i] = (Custom){dx, dy, scale};
        }
    }
    return 1;
}

void touch_settings_write(FILE *f)
{
    custom_init();
    for (int o = 0; o < 2; o++) {
        for (int i = 0; i < ITEM_COUNT; i++) {
            const Custom *c = &custom[o][i];
            if (c->dx != 0 || c->dy != 0 || c->scale != 1) {
                fprintf(f, "button.%s.%s=%.4f,%.4f,%.3f\n", o ? "wide" : "tall", item_names[i], c->dx, c->dy, c->scale);
            }
        }
    }
}

/* Layout ------------------------------------------------------------------------------------ */

static SDL_Rect rect(double x, double y, double w, double h)
{
    SDL_Rect r = {(int) x, (int) y, (int) w, (int) h};
    return r;
}

static void add_button(Layout *l, int item, uint32_t key, const char *label, int round, SDL_Rect r)
{
    if (l->button_count < LAYOUT_BUTTONS) {
        Button *b = &l->buttons[l->button_count++];
        b->key = key;
        b->label = label;
        b->round = round;
        b->item = item;
        b->over = 0;
        b->r = r;
    }
}

/* The four face buttons in a diamond around (cx, cy), each of radius r. */
static void add_face(Layout *l, double cx, double cy, double r)
{
    double d = r * 1.25;
    add_button(l, ITEM_FACE, DS_KEY_A, "A", 1, rect(cx + d - r, cy - r, 2 * r, 2 * r));
    add_button(l, ITEM_FACE, DS_KEY_B, "B", 1, rect(cx - r, cy + d - r, 2 * r, 2 * r));
    add_button(l, ITEM_FACE, DS_KEY_X, "X", 1, rect(cx - r, cy - d - r, 2 * r, 2 * r));
    add_button(l, ITEM_FACE, DS_KEY_Y, "Y", 1, rect(cx - d - r, cy - r, 2 * r, 2 * r));
}

/* An item's bounds: the pad's circle, the face buttons' four together, else the button. */
static SDL_Rect item_bounds(const Layout *l, int item)
{
    if (item == ITEM_PAD) {
        return rect(l->pad_x - l->pad_r, l->pad_y - l->pad_r, 2 * l->pad_r, 2 * l->pad_r);
    }
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0, any = 0;
    for (int i = 0; i < l->button_count; i++) {
        const SDL_Rect *r = &l->buttons[i].r;
        if (l->buttons[i].item != item) {
            continue;
        }
        if (!any || r->x < x0) {
            x0 = r->x;
        }
        if (!any || r->y < y0) {
            y0 = r->y;
        }
        if (!any || r->x + r->w > x1) {
            x1 = r->x + r->w;
        }
        if (!any || r->y + r->h > y1) {
            y1 = r->y + r->h;
        }
        any = 1;
    }
    return rect(x0, y0, x1 - x0, y1 - y0);
}

static int overlaps(const SDL_Rect *a, const SDL_Rect *b)
{
    return a->x < b->x + b->w && b->x < a->x + a->w && a->y < b->y + b->h && b->y < a->y + a->h;
}

/* The player's arrangement over the layout's own: each item about its centre. An item stays
 * within the window, at least by its centre. */
static void customize(Layout *l)
{
    custom_init();
    const Custom *set = custom[l->wide];
    double unit = l->w < l->h ? l->w : l->h;
    SDL_Rect picture = {l->top.x, l->top.y, l->top.w, l->top.h + l->bottom.h};
    for (int item = 0; item < ITEM_COUNT; item++) {
        const Custom *c = &set[item];
        SDL_Rect b = item_bounds(l, item);
        double cx = b.x + b.w / 2.0, cy = b.y + b.h / 2.0;
        double nx = cx + c->dx * unit, ny = cy + c->dy * unit;
        nx = nx < 0 ? 0 : nx > l->w ? l->w : nx;
        ny = ny < 0 ? 0 : ny > l->h ? l->h : ny;
        if (item == ITEM_PAD) {
            l->pad_x = (int) nx;
            l->pad_y = (int) ny;
            l->pad_r = (int) (l->pad_r * c->scale);
        } else {
            for (int i = 0; i < l->button_count; i++) {
                SDL_Rect *r = &l->buttons[i].r;
                if (l->buttons[i].item == item) {
                    double bx = r->x + r->w / 2.0, by = r->y + r->h / 2.0, w = r->w * c->scale, h = r->h * c->scale;
                    *r = rect(nx + (bx - cx) * c->scale - w / 2, ny + (by - cy) * c->scale - h / 2, w, h);
                }
            }
        }
        b = item_bounds(l, item);
        int over = overlaps(&b, &picture);
        if (item == ITEM_PAD) {
            l->pad_over = over;
        }
        for (int i = 0; i < l->button_count; i++) {
            if (l->buttons[i].item == item) {
                l->buttons[i].over = over;
            }
        }
    }
}

Layout layout_compute(int w, int h, int touch, int full_touch)
{
    Layout l;
    memset(&l, 0, sizeof(l));
    l.w = w;
    l.h = h;
    l.touch = touch;
    l.full_touch = touch && full_touch;
    const double sw = FRONT_SCREEN_W, sh = 2 * FRONT_SCREEN_H;
    int wide = w * sh >= h * sw; /* wider than the picture: its height is what limits it */
    int buttons = touch && !full_touch;
    l.wide = wide;

    double s = wide ? h / sh : w / sw;
    if (!touch && s >= 1.0) {
        s = floor(s);
    }
    double pw = sw * s, ph = sh * s;
    double x = (w - pw) / 2, y = (h - ph) / 2;
    /* a tall window with buttons: the picture at the top, the buttons in what is left */
    double below = h - ph, block = w * 0.6 < h * 0.3 ? w * 0.6 : h * 0.3;
    if (buttons && !wide) {
        y = 0;
    }
    l.top = rect(x, y, pw, ph / 2);
    l.bottom = rect(x, y + ph / 2, pw, ph - ph / 2);
    if (!touch) {
        return l;
    }

    /* the mark for the buttons: a corner of the room left over, else of the picture */
    double mark = (w < h ? w : h) * 0.075;
    l.toggle = rect(w - mark * 1.25, h - mark * 1.25, mark, mark);
    if (!buttons) {
        return l;
    }

    double side = x; /* the room on each side of the picture, in a wide window */
    if (wide && side >= h * 0.28) {
        /* the +Control Pad, L and SELECT on the left of the picture; the face buttons, R and
           START on the right */
        double unit = side < h * 0.5 ? side : h * 0.5;
        l.pad_r = (int) (unit * 0.36);
        l.pad_x = (int) (side / 2);
        l.pad_y = (int) (h * 0.6);
        add_face(&l, w - side / 2, h * 0.6, unit * 0.13);
        double bw = unit * 0.5, bh = unit * 0.16;
        add_button(&l, ITEM_L, DS_KEY_L, "L", 0, rect(side / 2 - bw / 2, h * 0.08, bw, bh));
        add_button(&l, ITEM_R, DS_KEY_R, "R", 0, rect(w - side / 2 - bw / 2, h * 0.08, bw, bh));
        add_button(&l, ITEM_SELECT, DS_KEY_SELECT, "SELECT", 0, rect(side / 2 - bw / 2, h * 0.88, bw, bh * 0.8));
        add_button(&l, ITEM_START, DS_KEY_START, "START", 0, rect(w - side / 2 - bw / 2, h * 0.88, bw, bh * 0.8));
        l.toggle = rect(w - side / 2 - mark / 2, h * 0.3, mark, mark);
    } else {
        /* a block of buttons across the window's width: in the room below the picture where
           it fits, else over the picture's lower part. L and R on top, the pad and the face
           buttons under them, SELECT, the mark and START between those */
        double top = h - block, rest = block;
        if (!wide && below >= block) {
            top = ph + (below - block) / 2;
        }
        double unit = rest < w * 0.5 ? rest : w * 0.5;
        double bw = w * 0.22, bh = unit * 0.13;
        add_button(&l, ITEM_L, DS_KEY_L, "L", 0, rect(w * 0.04, top + rest * 0.04, bw, bh));
        add_button(&l, ITEM_R, DS_KEY_R, "R", 0, rect(w * 0.96 - bw, top + rest * 0.04, bw, bh));
        l.pad_r = (int) (unit * 0.33);
        l.pad_x = (int) (w * 0.25);
        l.pad_y = (int) (top + rest * 0.6);
        add_face(&l, w * 0.75, top + rest * 0.6, unit * 0.12);
        double cw = w * 0.15, ch = bh * 0.75;
        add_button(&l, ITEM_SELECT, DS_KEY_SELECT, "SELECT", 0, rect(w / 2 - cw - mark, top + rest * 0.06, cw, ch));
        add_button(&l, ITEM_START, DS_KEY_START, "START", 0, rect(w / 2 + mark, top + rest * 0.06, cw, ch));
        l.toggle = rect(w / 2 - mark / 2, top + rest * 0.06 + ch / 2 - mark / 2, mark, mark);
    }
    customize(&l);
    return l;
}

/* Arranging ---------------------------------------------------------------------------------- */

/* Held this long (frames, about 0.6 s), the mark opens the arranging. */
#define HOLD_FRAMES 36

enum { BAR_SMALLER, BAR_LARGER, BAR_RESET, BAR_DONE, BAR_COUNT };

static struct {
    int on;
    int selected;       /* the item last touched, -1 none */
    int dragging;       /* a finger is moving it */
    int64_t finger;
    int last_x, last_y;
    /* the mark while a finger is on it */
    int mark_down, mark_fired;
    int64_t mark_finger;
    uint32_t mark_frame;
} arrange = {0, -1, 0, 0, 0, 0, 0, 0, 0, 0};

/* The arranging's own four controls, in a row across the upper part of the picture. */
static void bar_rects(const Layout *l, SDL_Rect r[BAR_COUNT])
{
    double unit = (l->w < l->h ? l->w : l->h) * 0.13, gap = unit * 0.25;
    double widths[BAR_COUNT] = {unit, unit, unit * 2.2, unit};
    double total = gap * (BAR_COUNT - 1), x;
    for (int i = 0; i < BAR_COUNT; i++) {
        total += widths[i];
    }
    x = (l->w - total) / 2;
    double y = l->top.y + l->top.h * 0.25;
    for (int i = 0; i < BAR_COUNT; i++) {
        r[i] = rect(x, y, widths[i], unit);
        x += widths[i] + gap;
    }
}

static int on_rect(const SDL_Rect *r, int x, int y)
{
    return x >= r->x && x < r->x + r->w && y >= r->y && y < r->y + r->h;
}

int touch_arranging(void)
{
    return arrange.on;
}

int touch_arrange_close(void)
{
    int was = arrange.on;
    arrange.on = 0;
    arrange.dragging = 0;
    arrange.selected = -1;
    return was;
}

int touch_held(uint32_t frame)
{
    if (arrange.mark_down && !arrange.mark_fired && frame - arrange.mark_frame >= HOLD_FRAMES) {
        arrange.mark_fired = 1;
        arrange.on = 1;
        arrange.selected = -1;
        return 1;
    }
    return 0;
}

/* A finger while arranging: the bar's controls, else the item under it, which follows it. */
static void arrange_finger(const Layout *l, TouchAction action, int64_t id, int x, int y, TouchPen *pen)
{
    Custom *set = custom[l->wide];
    double unit = l->w < l->h ? l->w : l->h;
    if (action == TOUCH_DOWN) {
        SDL_Rect bar[BAR_COUNT];
        bar_rects(l, bar);
        for (int i = 0; i < BAR_COUNT; i++) {
            if (!on_rect(&bar[i], x, y)) {
                continue;
            }
            if (i == BAR_DONE) {
                touch_arrange_close();
                pen->arranged = 1;
            } else if (i == BAR_RESET) {
                for (int k = 0; k < ITEM_COUNT; k++) {
                    set[k] = (Custom){0, 0, 1};
                }
            } else if (arrange.selected >= 0) {
                float s = set[arrange.selected].scale * (i == BAR_LARGER ? 1.1f : 1 / 1.1f);
                set[arrange.selected].scale = s < 0.4f ? 0.4f : s > 3.0f ? 3.0f : s;
            }
            return;
        }
        /* the smallest item under the finger: a small one inside a large one can be reached */
        int best = -1, best_area = 0;
        for (int item = 0; item < ITEM_COUNT; item++) {
            SDL_Rect b = item_bounds(l, item);
            int m = (int) (unit * 0.02);
            SDL_Rect grown = {b.x - m, b.y - m, b.w + 2 * m, b.h + 2 * m};
            if (on_rect(&grown, x, y) && (best < 0 || b.w * b.h < best_area)) {
                best = item;
                best_area = b.w * b.h;
            }
        }
        if (best >= 0) {
            arrange.selected = best;
            arrange.dragging = 1;
            arrange.finger = id;
            arrange.last_x = x;
            arrange.last_y = y;
        }
    } else if (arrange.dragging && arrange.finger == id) {
        if (action == TOUCH_MOVE) {
            set[arrange.selected].dx += (float) ((x - arrange.last_x) / unit);
            set[arrange.selected].dy += (float) ((y - arrange.last_y) / unit);
            arrange.last_x = x;
            arrange.last_y = y;
        } else {
            arrange.dragging = 0;
        }
    }
}

/* Fingers ------------------------------------------------------------------------------------ */

#define MAX_FINGERS 10

static struct {
    int used, pen;
    int64_t id;
    uint32_t keys;
} fingers[MAX_FINGERS];
static uint32_t taps;

static uint32_t pad_at(const Layout *l, int x, int y)
{
    double dx = x - l->pad_x, dy = y - l->pad_y;
    double d = sqrt(dx * dx + dy * dy);
    if (l->pad_r <= 0 || d > l->pad_r * 1.4 || d < l->pad_r * 0.18) {
        return 0;
    }
    /* eight directions of 45 degrees each, right first, going clockwise (y grows downwards) */
    static const uint32_t dirs[8] = {
        DS_KEY_RIGHT, DS_KEY_RIGHT | DS_KEY_DOWN, DS_KEY_DOWN, DS_KEY_DOWN | DS_KEY_LEFT,
        DS_KEY_LEFT,  DS_KEY_LEFT | DS_KEY_UP,    DS_KEY_UP,   DS_KEY_UP | DS_KEY_RIGHT,
    };
    double a = atan2(dy, dx); /* -pi..pi */
    int sector = (int) floor((a + M_PI / 8) / (M_PI / 4));
    return dirs[(sector % 8 + 8) % 8];
}

static uint32_t keys_at(const Layout *l, int x, int y)
{
    uint32_t keys = pad_at(l, x, y);
    for (int i = 0; i < l->button_count; i++) {
        const Button *b = &l->buttons[i];
        if (b->round) {
            double r = b->r.w / 2.0, dx = x - (b->r.x + r), dy = y - (b->r.y + r);
            if (dx * dx + dy * dy <= r * r * 1.3) {
                keys |= b->key;
            }
        } else {
            int m = b->r.h / 3; /* a little extra around the small buttons */
            if (x >= b->r.x - m && x < b->r.x + b->r.w + m && y >= b->r.y - m && y < b->r.y + b->r.h + m) {
                keys |= b->key;
            }
        }
    }
    return keys;
}

TouchPen touch_finger(const Layout *l, TouchAction action, int64_t id, int x, int y, uint32_t frame)
{
    TouchPen pen = {0, 0, x, y, 0, 0};
    /* the mark: a tap turns the buttons off or on, a hold opens the arranging (touch_held) */
    if (arrange.mark_down && arrange.mark_finger == id) {
        if (action == TOUCH_UP) {
            arrange.mark_down = 0;
            pen.toggle = !arrange.mark_fired;
        }
        return pen;
    }
    if (arrange.on) {
        arrange_finger(l, action, id, x, y, &pen);
        return pen;
    }
    int slot = -1, pen_taken = 0;
    for (int i = 0; i < MAX_FINGERS; i++) {
        if (fingers[i].used && fingers[i].id == id) {
            slot = i;
        }
        pen_taken |= fingers[i].used && fingers[i].pen;
    }
    if (action == TOUCH_DOWN) {
        if (l->touch && on_rect(&l->toggle, x, y)) {
            arrange.mark_down = 1;
            arrange.mark_fired = 0;
            arrange.mark_finger = id;
            arrange.mark_frame = frame;
            return pen;
        }
        if (slot < 0) {
            for (int i = 0; i < MAX_FINGERS && slot < 0; i++) {
                if (!fingers[i].used) {
                    slot = i;
                }
            }
        }
        if (slot < 0) {
            return pen;
        }
        /* a button under the finger comes first (the buttons may lie over the picture) */
        int on_button = !l->full_touch && keys_at(l, x, y) != 0;
        fingers[slot].used = 1;
        fingers[slot].id = id;
        fingers[slot].pen = !on_button && !pen_taken && on_rect(&l->bottom, x, y);
        fingers[slot].keys = 0;
    }
    if (slot < 0) {
        return pen;
    }
    if (fingers[slot].pen) {
        pen.changed = 1;
        pen.down = action != TOUCH_UP;
    } else {
        uint32_t keys = action == TOUCH_UP || l->full_touch ? 0 : keys_at(l, x, y);
        taps |= keys & ~fingers[slot].keys;
        fingers[slot].keys = keys;
    }
    if (action == TOUCH_UP) {
        fingers[slot].used = 0;
    }
    return pen;
}

uint32_t touch_keys(void)
{
    uint32_t keys = 0;
    for (int i = 0; i < MAX_FINGERS; i++) {
        if (fingers[i].used) {
            keys |= fingers[i].keys;
        }
    }
    return arrange.on ? 0 : keys;
}

uint32_t touch_taps(void)
{
    uint32_t t = taps;
    taps = 0;
    return arrange.on ? 0 : t;
}

/* Drawing ------------------------------------------------------------------------------------ */

/* 5 x 7 letters for the labels, a row a byte, the leftmost pixel in bit 4. */
static const struct {
    char c;
    uint8_t rows[7];
} glyphs[] = {
    {'A', {0x0e, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11}}, {'B', {0x1e, 0x11, 0x11, 0x1e, 0x11, 0x11, 0x1e}},
    {'C', {0x0f, 0x10, 0x10, 0x10, 0x10, 0x10, 0x0f}}, {'E', {0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f}},
    {'L', {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1f}}, {'R', {0x1e, 0x11, 0x11, 0x1e, 0x14, 0x12, 0x11}},
    {'S', {0x0f, 0x10, 0x10, 0x0e, 0x01, 0x01, 0x1e}}, {'T', {0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}},
    {'X', {0x11, 0x11, 0x0a, 0x04, 0x0a, 0x11, 0x11}}, {'Y', {0x11, 0x11, 0x0a, 0x04, 0x04, 0x04, 0x04}},
};

static void draw_text(SDL_Renderer *r, const char *text, int cx, int cy, int px)
{
    if (px < 1) {
        px = 1;
    }
    int n = (int) strlen(text);
    int x0 = cx - (n * 6 * px - px) / 2, y0 = cy - 7 * px / 2;
    for (int i = 0; i < n; i++) {
        for (size_t g = 0; g < sizeof(glyphs) / sizeof(glyphs[0]); g++) {
            if (glyphs[g].c != text[i]) {
                continue;
            }
            for (int row = 0; row < 7; row++) {
                for (int col = 0; col < 5; col++) {
                    if (glyphs[g].rows[row] & (0x10 >> col)) {
                        SDL_Rect p = {x0 + (i * 6 + col) * px, y0 + row * px, px, px};
                        SDL_RenderFillRect(r, &p);
                    }
                }
            }
        }
    }
}

static void fill_circle(SDL_Renderer *r, int cx, int cy, int radius, SDL_Color c)
{
    enum { SEGMENTS = 40 };
    SDL_Vertex v[SEGMENTS + 1];
    int idx[SEGMENTS * 3];
    v[0].position.x = (float) cx;
    v[0].position.y = (float) cy;
    v[0].color = c;
    for (int i = 0; i < SEGMENTS; i++) {
        double a = 2 * M_PI * i / SEGMENTS;
        v[i + 1].position.x = (float) (cx + radius * cos(a));
        v[i + 1].position.y = (float) (cy + radius * sin(a));
        v[i + 1].color = c;
        idx[3 * i] = 0;
        idx[3 * i + 1] = i + 1;
        idx[3 * i + 2] = i + 1 == SEGMENTS ? 1 : i + 2;
    }
    SDL_RenderGeometry(r, NULL, v, SEGMENTS + 1, idx, SEGMENTS * 3);
}

/* A line of the given thickness, as a quad. */
static void thick_line(SDL_Renderer *r, double x0, double y0, double x1, double y1, double thick, SDL_Color c)
{
    double dx = x1 - x0, dy = y1 - y0, len = sqrt(dx * dx + dy * dy);
    if (len <= 0) {
        return;
    }
    double nx = -dy / len * thick / 2, ny = dx / len * thick / 2;
    SDL_Vertex v[4] = {{{(float) (x0 + nx), (float) (y0 + ny)}, c, {0, 0}},
                       {{(float) (x1 + nx), (float) (y1 + ny)}, c, {0, 0}},
                       {{(float) (x1 - nx), (float) (y1 - ny)}, c, {0, 0}},
                       {{(float) (x0 - nx), (float) (y0 - ny)}, c, {0, 0}}};
    static const int idx[6] = {0, 1, 2, 0, 2, 3};
    SDL_RenderGeometry(r, NULL, v, 4, idx, 6);
}

/* White, more of it while pressed. Over the picture a button is a dark patch instead, with
 * its letter in white, so that it shows on the game's light panels as on its dark scenes. */
static SDL_Color shade(int pressed, int over)
{
    SDL_Color c = {255, 255, 255, (Uint8) (pressed ? 150 : 55)};
    if (over && !pressed) {
        c = (SDL_Color){0, 0, 0, 70};
    }
    return c;
}

static void outline(SDL_Renderer *r, SDL_Rect b, int selected)
{
    SDL_SetRenderDrawColor(r, 255, selected ? 220 : 255, selected ? 60 : 255, (Uint8) (selected ? 255 : 130));
    for (int k = 0; k < (selected ? 4 : 2); k++) {
        SDL_Rect o = {b.x - 6 - k, b.y - 6 - k, b.w + 12 + 2 * k, b.h + 12 + 2 * k};
        SDL_RenderDrawRect(r, &o);
    }
}

/* The arranging: every item framed, the one last touched in yellow, and the bar: smaller,
 * larger, RESET (all back where the layout puts them) and the tick that closes it. */
static void draw_arranging(SDL_Renderer *r, const Layout *l)
{
    for (int item = 0; item < ITEM_COUNT; item++) {
        outline(r, item_bounds(l, item), item == arrange.selected);
    }
    SDL_Rect bar[BAR_COUNT];
    bar_rects(l, bar);
    SDL_Color white = {255, 255, 255, 255};
    for (int i = 0; i < BAR_COUNT; i++) {
        int dim = (i == BAR_SMALLER || i == BAR_LARGER) && arrange.selected < 0;
        SDL_SetRenderDrawColor(r, 20, 20, 20, 200);
        SDL_RenderFillRect(r, &bar[i]);
        SDL_SetRenderDrawColor(r, 255, 255, 255, (Uint8) (dim ? 90 : 255));
        SDL_RenderDrawRect(r, &bar[i]);
        int cx = bar[i].x + bar[i].w / 2, cy = bar[i].y + bar[i].h / 2, u = bar[i].h;
        white.a = (Uint8) (dim ? 90 : 255);
        if (i == BAR_SMALLER || i == BAR_LARGER) {
            thick_line(r, cx - u * 0.28, cy, cx + u * 0.28, cy, u * 0.1, white);
            if (i == BAR_LARGER) {
                thick_line(r, cx, cy - u * 0.28, cx, cy + u * 0.28, u * 0.1, white);
            }
        } else if (i == BAR_RESET) {
            draw_text(r, "RESET", cx, cy, bar[i].w * 8 / 10 / 30);
        } else {
            thick_line(r, cx - u * 0.28, cy, cx - u * 0.08, cy + u * 0.2, u * 0.1, white);
            thick_line(r, cx - u * 0.08, cy + u * 0.2, cx + u * 0.3, cy - u * 0.22, u * 0.1, white);
        }
    }
}

void touch_draw(SDL_Renderer *r, const Layout *l, uint32_t held)
{
    /* the mark for the buttons: three dots, fainter when it is all that is drawn */
    if (l->touch && l->toggle.w > 0) {
        int d = l->toggle.w / 7, cy = l->toggle.y + l->toggle.h / 2;
        SDL_Color c = {255, 255, 255, (Uint8) (l->full_touch ? 90 : 140)}, under = {0, 0, 0, 60};
        fill_circle(r, l->toggle.x + l->toggle.w / 2, cy, l->toggle.w * 2 / 3, under);
        for (int i = -1; i <= 1; i++) {
            fill_circle(r, l->toggle.x + l->toggle.w / 2 + i * (l->toggle.w / 3), cy, d > 2 ? d : 2, c);
        }
    }
    if (l->full_touch) {
        return;
    }
    /* the +Control Pad: a disc, and an arm for each direction */
    if (l->pad_r > 0) {
        fill_circle(r, l->pad_x, l->pad_y, l->pad_r, shade(0, l->pad_over));
        int arm = l->pad_r * 2 / 3, thick = l->pad_r * 2 / 3;
        static const struct {
            uint32_t key;
            int dx, dy;
        } arms[4] = {{DS_KEY_UP, 0, -1}, {DS_KEY_DOWN, 0, 1}, {DS_KEY_LEFT, -1, 0}, {DS_KEY_RIGHT, 1, 0}};
        for (int i = 0; i < 4; i++) {
            SDL_Rect a;
            if (arms[i].dx == 0) {
                a = (SDL_Rect){l->pad_x - thick / 2, arms[i].dy < 0 ? l->pad_y - arm - thick / 2 : l->pad_y + thick / 2,
                               thick, arm};
            } else {
                a = (SDL_Rect){arms[i].dx < 0 ? l->pad_x - arm - thick / 2 : l->pad_x + thick / 2, l->pad_y - thick / 2,
                               arm, thick};
            }
            SDL_Color c = shade((held & arms[i].key) != 0, l->pad_over);
            SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
            SDL_RenderFillRect(r, &a);
        }
    }
    for (int i = 0; i < l->button_count; i++) {
        const Button *b = &l->buttons[i];
        SDL_Color c = shade((held & b->key) != 0, b->over);
        int cx = b->r.x + b->r.w / 2, cy = b->r.y + b->r.h / 2;
        if (b->round) {
            fill_circle(r, cx, cy, b->r.w / 2, c);
        } else {
            SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
            SDL_RenderFillRect(r, &b->r);
        }
        int px = (b->round ? b->r.h / 2 : b->r.h) / 9;
        if (!b->round && (int) strlen(b->label) * 6 * px > b->r.w * 9 / 10) {
            px = b->r.w * 9 / 10 / ((int) strlen(b->label) * 6);
        }
        SDL_SetRenderDrawColor(r, 255, 255, 255, (Uint8) (b->over ? 190 : 220));
        draw_text(r, b->label, cx, cy, px);
    }
    if (arrange.on) {
        draw_arranging(r, l);
    }
}
