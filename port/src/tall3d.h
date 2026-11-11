/* The picture beyond the game's own: the 3D scene continued above the lower screen (the tall
 * view), and the scene drawn larger than the DS draws it.
 *
 * The game draws its 3D scene on one screen, 256 x 192. Here further copies of the 3D engine
 * (gpu3d_tile.c) are given the same commands with the projection changed, each to see one
 * part of a larger view as its whole screen:
 *
 *   - the tall view: the part above the lower screen's top edge (y' = y - 2w in clip
 *     coordinates), which the front end lays over the upper screen;
 *   - a larger size: the view cut into tiles of the DS's size, scale x scale to a screen,
 *     each tile's copy zoomed onto it (x' = scale * x + ox * w, y' = scale * y + oy * w). The
 *     tiles side by side are the scene at `scale` times the DS's size, drawn by the DS's own
 *     rules: every pixel is the engine's.
 *
 * The game and its frames are untouched: the main engine gets the commands as they are and
 * draws the picture the game sees, and nothing reads the copies but the front end.
 *
 * What it cannot add is what the game does not send: what the game itself leaves out because
 * it is off its screen (game_hooks.c widens that for the game's objects).
 */
#ifndef ACWW_PORT_TALL3D_H
#define ACWW_PORT_TALL3D_H

#include <stdint.h>

#include "scene.h"

#define TALL3D_W 256
#define TALL3D_H 192

/* Before the run starts: the tall view or not, and the size (1 to 3 times the DS's). At size
 * 1 without the tall view no copy runs, and the cost is nothing. */
void tall3d_enable(int tall_view, int scale);

/* Instead of that: the scene, whole (two screens high with the tall view), handed to `sink`
 * frame by frame for another renderer to draw (scene.h, docs/renderer.md). */
void tall3d_enable_scene(int tall_view, SceneSink sink, void *user);

/* With the scene handed out: whether the frame being drawn shows the 3D layer on the lower
 * screen (else there is no scene to show), whether it goes on over the upper screen, and the
 * lower screen's master brightness. For the drawing thread. */
int tall3d_scene_state(int *over_upper, uint32_t *brightness);
/* With the scene handed out (tall3d_enable_scene): whether the main engine's own picture may
 * be left undrawn while the scene's is the one shown. The game's frames are then no longer
 * the decomp's where the 3D layer is (its lines stay as they last were), so not for a run
 * that records or compares them. Off at the start. */
void tall3d_lean(int on);
/* how many of the main engine's pictures were left undrawn, and drawn, since the start */
void tall3d_lean_counts(unsigned long *left, unsigned long *drawn);
int tall3d_scale(void);

/* For the frame the drawing thread has just finished (call it from frame_output_hook): the
 * copies' picture that goes with the 3D picture on the lower screen. Returns 0 when the
 * lower screen is not showing the 3D picture. Else `*upper` is the upper screen's part
 * (NULL without the tall view, or during a wipe that hides the layer outside its windows)
 * and `*lower` the lower screen's (NULL at size 1, where the game's own picture is the one):
 * each 256 * scale pixels wide and 192 * scale high, rows `*stride` pixels apart, a pixel
 * red | green << 8 | blue << 16 (6 bits each) | alpha << 24 (5 bits, 0 where nothing was
 * drawn). `*brightness` is the main engine's MASTER_BRIGHT, which the upper part is to be
 * shown with. */
int tall3d_picture(const uint32_t **upper, const uint32_t **lower, int *stride, uint32_t *brightness);

#endif
