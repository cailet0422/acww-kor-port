/* Buttons drawn on a touch screen, and the fingers on them. A finger that lands on the lower
 * screen is the stylus until it lifts (one at a time); any other finger presses the buttons
 * under it, and can slide from one to the next. */
#ifndef ACWW_PORT_TOUCH_H
#define ACWW_PORT_TOUCH_H

#include <SDL.h>
#include <stdint.h>

#include "front.h"

typedef enum TouchAction { TOUCH_DOWN, TOUCH_MOVE, TOUCH_UP } TouchAction;

typedef struct TouchPen {
    int changed; /* the stylus moved, landed or lifted */
    int down;
    int x, y;    /* in window pixels (front_sdl.c maps them onto the lower screen) */
} TouchPen;

TouchPen touch_finger(const Layout *l, TouchAction action, int64_t finger, int x, int y);

/* the buttons the fingers hold now */
uint32_t touch_keys(void);
/* the buttons pressed since the last call */
uint32_t touch_taps(void);

void touch_draw(SDL_Renderer *r, const Layout *l, uint32_t held);

#endif
