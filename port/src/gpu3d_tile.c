/* One more copy of the DS's 3D engine (decomp/runtime/gpu3d.c, unchanged), built once for
 * each TILE: the engines that draw the parts of the picture the game's own engine does not
 * (tall3d.h): the tall view's upper part, and at a larger size every tile of the picture.
 * Every name a copy defines is its own. It asks for no drawing itself (tall3d.c draws it with
 * the main engine's list), leaves the I/O registers alone, and reads no settings from the
 * environment. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tall3d_engine.h"

#define TILE_NAME_(n, name) gpu3d_tile##n##_##name
#define TILE_NAME(n, name) TILE_NAME_(n, name)
#define T(name) TILE_NAME(TILE, name)

#define gpu3d_capture_start T(capture_start)
#define gpu3d_capture_write T(capture_write)
#define gpu3d_dot_depth_write T(dot_depth_write)
#define gpu3d_dump_png T(dump_png)
#define gpu3d_generation T(generation)
#define gpu3d_gxstat_write T(gxstat_write)
#define gpu3d_init T(init)
#define gpu3d_line T(line)
#define gpu3d_line_rgb6 T(line_rgb6)
#define gpu3d_read T(read)
#define gpu3d_render T(render)
#define gpu3d_report T(report)
#define gpu3d_vblank T(vblank)
#define gpu3d_write T(write)

#define render_3d tall3d_tile_render_3d
#define io_reg_store tall3d_tile_reg_store
#define io_view_reg tall3d_tile_view_reg
char *tall3d_tile_getenv(const char *name);
#define getenv tall3d_tile_getenv

#include "gpu3d.c"

/* Before the main engine asks for the drawing of a swap: this engine's list for it, complete
 * (the swap's own sort flag set as its vblank will set it), or -1 without a swap pending. */
static int T(prepare)(void)
{
    if (!ge.swap_pending) {
        return -1;
    }
    lists[geometry_list].manual_sort = (int) ge.flush_attr & 1;
    return geometry_list;
}

const Engine3d T(engine) = {
    T(init), T(write), T(dot_depth_write), T(gxstat_write), T(vblank), T(render), T(line_rgb6), T(prepare),
};
