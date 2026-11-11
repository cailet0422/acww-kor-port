/* The tall view: the 3D scene continued above the lower screen, so the two screens show one
 * picture instead of a picture and a sky.
 *
 * The game draws its 3D scene on one screen (256 x 192). Here a second copy of the 3D engine
 * (gpu3d_upper.c) is given the same commands with the projection moved up by one screen
 * (y' = y - 2w in clip coordinates: what was just above the top edge is now inside), and
 * draws the part of the scene the game's own picture ends below. The front end lays that over
 * the upper screen. The game and its frames are untouched: the main engine gets the commands
 * as they are, and nothing reads the second engine but the front end.
 *
 * What it cannot add is what the game does not send: objects the game itself leaves out
 * because they are off its screen.
 */
#ifndef ACWW_PORT_TALL3D_H
#define ACWW_PORT_TALL3D_H

#include <stdint.h>

#define TALL3D_W 256
#define TALL3D_H 192

/* Before the run starts: whether the second engine runs at all. Off, the cost is nothing. */
void tall3d_enable(int on);

/* For the frame the drawing thread has just finished (call it from frame_output_hook): the
 * upper part that goes with the 3D picture on the lower screen, as the engine gives it,
 * red | green << 8 | blue << 16 (6 bits each) | alpha << 24 (5 bits, 0 where nothing was
 * drawn), with the main engine's master brightness applied. Returns 0 when the lower screen
 * is not showing the 3D picture (nothing to lay over). */
int tall3d_frame(uint32_t pixels[TALL3D_H][TALL3D_W]);

#endif
