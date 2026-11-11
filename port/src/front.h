/* What the front end (front_sdl.c) and the touch buttons (touch.c) share. */
#ifndef ACWW_PORT_FRONT_H
#define ACWW_PORT_FRONT_H

#include <SDL.h>
#include <stdint.h>

#define FRONT_SCREEN_W 256
#define FRONT_SCREEN_H 192

/* the DS's buttons as the runtime's input takes them (decomp/runtime/input.h, play.c) */
enum {
    DS_KEY_A = 1u << 0,
    DS_KEY_B = 1u << 1,
    DS_KEY_SELECT = 1u << 2,
    DS_KEY_START = 1u << 3,
    DS_KEY_RIGHT = 1u << 4,
    DS_KEY_LEFT = 1u << 5,
    DS_KEY_UP = 1u << 6,
    DS_KEY_DOWN = 1u << 7,
    DS_KEY_R = 1u << 8,
    DS_KEY_L = 1u << 9,
    DS_KEY_X = 1u << 10,
    DS_KEY_Y = 1u << 11,
};

typedef struct Button {
    uint32_t key;
    const char *label;
    int round;      /* a circle around (x + w / 2, y + h / 2), else a rounded-off rectangle */
    SDL_Rect r;
} Button;

#define LAYOUT_BUTTONS 8

/* Where everything goes in a window of w x h pixels. */
typedef struct Layout {
    int w, h;
    SDL_Rect top, bottom; /* the DS's screens, the upper right above the lower: one picture */
    int touch;            /* a touch screen: the buttons can be drawn and touched */
    int full_touch;       /* the stylus alone: no buttons drawn */
    int over;             /* the buttons lie over the picture (no room beside or below it) */
    int pad_x, pad_y, pad_r; /* the +Control Pad: its centre and reach */
    Button buttons[LAYOUT_BUTTONS];
    int button_count;
    SDL_Rect toggle;      /* the small mark that turns the buttons on and off */
} Layout;

/* The picture (both screens, one above the other) as large as the window lets it be: across
 * the whole width of a tall window, the whole height of a wide one; without a touch screen,
 * in whole multiples of the DS's size where that is at least 1. With `touch` and not
 * `full_touch`, the buttons go where there is room, below the picture in a tall window and
 * beside it in a wide one, and over its lower part where there is none. */
Layout layout_compute(int w, int h, int touch, int full_touch);

#endif
