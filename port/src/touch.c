#include "touch.h"

#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Layout ------------------------------------------------------------------------------------ */

static SDL_Rect rect(double x, double y, double w, double h)
{
    SDL_Rect r = {(int) x, (int) y, (int) w, (int) h};
    return r;
}

static void add_button(Layout *l, uint32_t key, const char *label, int round, SDL_Rect r)
{
    if (l->button_count < LAYOUT_BUTTONS) {
        Button *b = &l->buttons[l->button_count++];
        b->key = key;
        b->label = label;
        b->round = round;
        b->r = r;
    }
}

/* The four face buttons in a diamond around (cx, cy), each of radius r. */
static void add_face(Layout *l, double cx, double cy, double r)
{
    double d = r * 1.25;
    add_button(l, DS_KEY_A, "A", 1, rect(cx + d - r, cy - r, 2 * r, 2 * r));
    add_button(l, DS_KEY_B, "B", 1, rect(cx - r, cy + d - r, 2 * r, 2 * r));
    add_button(l, DS_KEY_X, "X", 1, rect(cx - r, cy - d - r, 2 * r, 2 * r));
    add_button(l, DS_KEY_Y, "Y", 1, rect(cx - d - r, cy - r, 2 * r, 2 * r));
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
        add_button(&l, DS_KEY_L, "L", 0, rect(side / 2 - bw / 2, h * 0.08, bw, bh));
        add_button(&l, DS_KEY_R, "R", 0, rect(w - side / 2 - bw / 2, h * 0.08, bw, bh));
        add_button(&l, DS_KEY_SELECT, "SELECT", 0, rect(side / 2 - bw / 2, h * 0.88, bw, bh * 0.8));
        add_button(&l, DS_KEY_START, "START", 0, rect(w - side / 2 - bw / 2, h * 0.88, bw, bh * 0.8));
        l.toggle = rect(w - side / 2 - mark / 2, h * 0.3, mark, mark);
        return l;
    }

    /* a block of buttons across the window's width: in the room below the picture where it
       fits, else over the picture's lower part. L and R on top, the pad and the face buttons
       under them, SELECT, the mark and START between those */
    double top = h - block, rest = block;
    if (!wide && below >= block) {
        top = ph + (below - block) / 2;
    } else {
        l.over = 1;
    }
    double unit = rest < w * 0.5 ? rest : w * 0.5;
    double bw = w * 0.22, bh = unit * 0.13;
    add_button(&l, DS_KEY_L, "L", 0, rect(w * 0.04, top + rest * 0.04, bw, bh));
    add_button(&l, DS_KEY_R, "R", 0, rect(w * 0.96 - bw, top + rest * 0.04, bw, bh));
    l.pad_r = (int) (unit * 0.33);
    l.pad_x = (int) (w * 0.25);
    l.pad_y = (int) (top + rest * 0.6);
    add_face(&l, w * 0.75, top + rest * 0.6, unit * 0.12);
    double cw = w * 0.15, ch = bh * 0.75;
    add_button(&l, DS_KEY_SELECT, "SELECT", 0, rect(w / 2 - cw - mark, top + rest * 0.06, cw, ch));
    add_button(&l, DS_KEY_START, "START", 0, rect(w / 2 + mark, top + rest * 0.06, cw, ch));
    l.toggle = rect(w / 2 - mark / 2, top + rest * 0.06 + ch / 2 - mark / 2, mark, mark);
    return l;
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

static int on_screen(const SDL_Rect *r, int x, int y)
{
    return x >= r->x && x < r->x + r->w && y >= r->y && y < r->y + r->h;
}

TouchPen touch_finger(const Layout *l, TouchAction action, int64_t id, int x, int y)
{
    TouchPen pen = {0, 0, x, y, 0};
    int slot = -1, pen_taken = 0;
    for (int i = 0; i < MAX_FINGERS; i++) {
        if (fingers[i].used && fingers[i].id == id) {
            slot = i;
        }
        pen_taken |= fingers[i].used && fingers[i].pen;
    }
    if (action == TOUCH_DOWN) {
        if (l->touch && on_screen(&l->toggle, x, y)) {
            pen.toggle = 1;
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
        fingers[slot].pen = !on_button && !pen_taken && on_screen(&l->bottom, x, y);
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
    return keys;
}

uint32_t touch_taps(void)
{
    uint32_t t = taps;
    taps = 0;
    return t;
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

/* White, more of it while pressed. Over the picture a button is a dark patch instead, with
 * its letter in white, so that it shows on the game's light panels as on its dark scenes. */
static int shade_over;

static SDL_Color shade(int pressed)
{
    SDL_Color c = {255, 255, 255, (Uint8) (pressed ? 150 : 55)};
    if (shade_over && !pressed) {
        c = (SDL_Color){0, 0, 0, 70};
    }
    return c;
}

void touch_draw(SDL_Renderer *r, const Layout *l, uint32_t held)
{
    shade_over = l->over;
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
        fill_circle(r, l->pad_x, l->pad_y, l->pad_r, shade(0));
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
            SDL_Color c = shade((held & arms[i].key) != 0);
            SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
            SDL_RenderFillRect(r, &a);
        }
    }
    for (int i = 0; i < l->button_count; i++) {
        const Button *b = &l->buttons[i];
        SDL_Color c = shade((held & b->key) != 0);
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
        SDL_SetRenderDrawColor(r, 255, 255, 255, (Uint8) (l->over ? 190 : 220));
        draw_text(r, b->label, cx, cy, px);
    }
}
