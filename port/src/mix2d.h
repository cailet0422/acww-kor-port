/* The 2D engines' part in a larger 3D picture.
 *
 * The game's 3D picture is one layer of the main 2D engine, which lays its other layers over
 * and under it and blends them by rules set for each frame and line (priorities, windows,
 * alpha, brightness), all at the DS's size. To show a 3D picture drawn larger, that work has
 * to be done at the larger size: what the 2D engine makes of a pixel must be known for any
 * colour the 3D layer may have there, not only for the one colour of the DS-sized picture.
 *
 * Everything the engine does to a layer's colour is linear in it. So each line of the main
 * engine is composed three more times (gpu2d_main.c), with a 3D layer that is all black, all
 * white, and absent; the line's backgrounds, objects and windows are drawn once, for all
 * four. From the three answers B, W and N at a pixel, a 3D colour c with alpha a gives
 *
 *     N                           where the 3D picture has nothing (a = 0)
 *     B + (W - B) * c             where it is opaque
 *     between the two by a        at its translucent pixels and smoothed edges
 *
 * for each of red, green and blue. Where the engine does not show the 3D layer at all (a
 * window, a layer in front) the three answers are the same, and so is the result. The game's
 * own lines are made as ever, before the answers: the frames the game is checked by.
 */
#ifndef ACWW_PORT_MIX2D_H
#define ACWW_PORT_MIX2D_H

#include <stdint.h>

/* Before the run starts (or before its first picture): whether the answers are made. */
void mix2d_enable(int on);

/* For the frame the drawing thread has just finished: the three answers for the main engine's
 * picture, 256 x 192 pixels each of red | green << 6 | blue << 12 (6 bits each). Returns 0
 * when they are not made. */
int mix2d_answers(const uint32_t **black, const uint32_t **white, const uint32_t **none);

#endif
