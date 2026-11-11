/* The DS's 2D engines: decomp/runtime/gpu2d.c as it is, with the entry points the mix of a
 * larger 3D picture needs to stand in front of given other names (mix2d.c has the functions
 * of the original names, which call these). Nothing of the engines changes. */
#define gpu2d_init gpu2d_main_init
#define gpu2d_line gpu2d_main_line
#define gpu2d_register_written gpu2d_main_register_written

#include "gpu2d.c"
