/* Buttons drawn on a touch screen, and the fingers on them. A finger that lands on a button
 * presses it, and can slide from one button to the next; one that lands on the lower screen
 * instead is the stylus until it lifts (one at a time). In the full-touch mode there are no
 * buttons, only the stylus.
 *
 * The mark with three dots opens the settings screen where there is one (Android), and the
 * arranging of the buttons is opened from there. Elsewhere a tap on it turns the buttons off
 * and on, and a hold opens the arranging. Arranging, each button can be dragged anywhere, the
 * window over the picture included, and made smaller or larger; what the player arranges is
 * kept for tall and for wide windows apart, as "button." lines of the settings.
 */
#ifndef ACWW_PORT_TOUCH_H
#define ACWW_PORT_TOUCH_H

#include <SDL.h>
#include <stdint.h>
#include <stdio.h>

#include "front.h"

typedef enum TouchAction { TOUCH_DOWN, TOUCH_MOVE, TOUCH_UP } TouchAction;

typedef struct TouchPen {
    int changed; /* the stylus moved, landed or lifted */
    int down;
    int x, y;    /* in window pixels (front_sdl.c maps them onto the lower screen) */
    int toggle;  /* the mark was tapped: buttons off or on */
    int arranged; /* the arranging was closed: the settings are to be saved */
    int settings; /* the mark was tapped where there is a settings screen: it is to be opened */
} TouchPen;

/* A finger landing, moving or lifting at (x, y) in window pixels, in frame `frame`. */
TouchPen touch_finger(const Layout *l, TouchAction action, int64_t finger, int x, int y, uint32_t frame);

/* Once a frame: 1 in the frame the mark has been held long enough, which opens the arranging
 * (the buttons must be shown from then on). */
int touch_held(uint32_t frame);

/* Whether the buttons are being arranged, and closing it (Android's Back): 1 if it was open. */
int touch_arranging(void);
int touch_arrange_close(void);

/* Whether there is a settings screen (Android): then a tap on the mark asks for it
 * (TouchPen.settings), and the arranging is opened from there (touch_arrange_open). */
void touch_settings_screen(int on);
void touch_arrange_open(void);

/* the buttons the fingers hold now */
uint32_t touch_keys(void);
/* the buttons pressed since the last call */
uint32_t touch_taps(void);

/* The player's arrangement in the settings: a line read (1 if it was one of these), and all
 * of them written. */
int touch_settings_line(const char *line);
void touch_settings_write(FILE *f);

void touch_draw(SDL_Renderer *r, const Layout *l, uint32_t held);

#endif
