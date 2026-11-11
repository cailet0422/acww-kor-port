/* A second copy of the DS's 3D engine (decomp/runtime/gpu3d.c, unchanged), for the tall view
 * (tall3d.h): it is given the game's 3D commands with the projection moved one screen up, and
 * so draws what lies above the lower screen's picture. Every name it defines is its own, it
 * asks for no drawing itself (tall3d.c draws it with the main engine's list), it leaves the
 * I/O registers alone, and it reads no settings from the environment. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define gpu3d_capture_start gpu3d_upper_capture_start
#define gpu3d_capture_write gpu3d_upper_capture_write
#define gpu3d_dot_depth_write gpu3d_upper_dot_depth_write
#define gpu3d_dump_png gpu3d_upper_dump_png
#define gpu3d_generation gpu3d_upper_generation
#define gpu3d_gxstat_write gpu3d_upper_gxstat_write
#define gpu3d_init gpu3d_upper_init
#define gpu3d_line gpu3d_upper_line
#define gpu3d_line_rgb6 gpu3d_upper_line_rgb6
#define gpu3d_read gpu3d_upper_read
#define gpu3d_render gpu3d_upper_render
#define gpu3d_report gpu3d_upper_report
#define gpu3d_vblank gpu3d_upper_vblank
#define gpu3d_write gpu3d_upper_write

#define render_3d tall3d_upper_render_3d
#define io_reg_store tall3d_upper_reg_store
#define io_view_reg tall3d_upper_view_reg
char *tall3d_upper_getenv(const char *name);
#define getenv tall3d_upper_getenv

#include "gpu3d.c"
