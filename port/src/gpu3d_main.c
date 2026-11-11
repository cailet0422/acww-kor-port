/* The DS's 3D engine: decomp/runtime/gpu3d.c as it is, with the entry points the tall view
 * needs to stand in front of given other names (tall3d.c has the functions of the original
 * names, which call these). Nothing of the engine changes: the frames are the decomp's. */
#define gpu3d_init gpu3d_main_init
#define gpu3d_write gpu3d_main_write
#define gpu3d_dot_depth_write gpu3d_main_dot_depth_write
#define gpu3d_gxstat_write gpu3d_main_gxstat_write
#define gpu3d_vblank gpu3d_main_vblank
#define gpu3d_render gpu3d_main_render
#define gpu3d_line_rgb6 gpu3d_main_line_rgb6
/* its request for a list's drawing, at a swap: the second engine swaps inside it */
#define render_3d tall3d_main_render_3d

#include "gpu3d.c"
