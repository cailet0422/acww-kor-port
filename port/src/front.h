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
    SDL_Rect top, bottom; /* the DS's screens */
    int touch;            /* the buttons are drawn and touched */
    int pad_x, pad_y, pad_r; /* the +Control Pad: its centre and reach */
    Button buttons[LAYOUT_BUTTONS];
    int button_count;
} Layout;

/* The screens stacked, as large as they fit: whole multiples of their size where that is
 * at least 1, so pixels stay square. With `touch`, room is left for the buttons: beside the
 * screens in a wide window, below them in a tall one. */
Layout layout_compute(int w, int h, int touch);

#endif
