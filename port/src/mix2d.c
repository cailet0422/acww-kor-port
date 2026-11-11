#include "mix2d.h"

#include <stddef.h>

#include "gpu2d.h"

/* the engines (gpu2d_main.c): the game's own lines, and the main engine's three answers with
 * flat 3D layers, composed after each of its lines */
void gpu2d_main_init(void);
void gpu2d_main_line(uint32_t y);
void gpu2d_main_line_probed(uint32_t y);
void gpu2d_main_register_written(uint32_t offset, uint32_t value);
const uint32_t *gpu2d_main_probe_frame(int probe);

static int enabled;

void mix2d_enable(int on)
{
    enabled = on;
}

/* The engines' entry points (runtime/gpu2d.h) --------------------------------------------- */

void gpu2d_init(void)
{
    gpu2d_main_init();
}

void gpu2d_line(uint32_t y)
{
    if (enabled) {
        gpu2d_main_line_probed(y);
    } else {
        gpu2d_main_line(y);
    }
}

void gpu2d_register_written(uint32_t offset, uint32_t value)
{
    gpu2d_main_register_written(offset, value);
}

int mix2d_answers(const uint32_t **black, const uint32_t **white, const uint32_t **none)
{
    if (!enabled) {
        return 0;
    }
    *black = gpu2d_main_probe_frame(0);
    *white = gpu2d_main_probe_frame(1);
    *none = gpu2d_main_probe_frame(2);
    return 1;
}
