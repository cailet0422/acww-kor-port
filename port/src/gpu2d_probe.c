/* One more copy of the DS's 2D engines (decomp/runtime/gpu2d.c, unchanged), built once for
 * each PROBE: it draws the main engine's lines as the game's own copy does, from the same
 * registers and video memory, but with a 3D layer of one flat colour (mix2d.h says why).
 * It never captures the display (that writes video memory) and never outputs a frame. */
#include <stdint.h>
#include <string.h>

#define PROBE_NAME_(n, name) gpu2d_probe##n##_##name
#define PROBE_NAME(n, name) PROBE_NAME_(n, name)
#define P(name) PROBE_NAME(PROBE, name)

#define gpu2d_capture_end P(capture_end)
#define gpu2d_frame_output P(frame_output)
#define gpu2d_frames P(frames)
#define gpu2d_init P(init)
#define gpu2d_line P(line)
#define gpu2d_redraw P(redraw)
#define gpu2d_register_written P(register_written)
#define gpu2d_screen P(screen)

/* its 3D layer: mix2d.c's flat lines */
#define gpu3d_generation mix2d_probe_generation
#define gpu3d_line_rgb6 P(layer_line)
#define gpu3d_line mix2d_probe_capture_line
#define frame_output mix2d_probe_frame_output
#define io_reg_store mix2d_probe_reg_store

const uint32_t *mix2d_probe_layer(int probe, int y);
const uint32_t *P(layer_line)(int y)
{
    return mix2d_probe_layer(PROBE, y);
}

#include "gpu2d.c"

/* Line y of the main engine alone, as gpu2d_line draws it but for the display capture. */
void P(main_line)(uint32_t y)
{
    if (y >= GPU2D_HEIGHT) {
        return;
    }
    Engine *e = &engines[0];
    uint32_t dispcnt = reg32(e, R_DISPCNT);
    if (y == 0) {
        e->ref_reload[0] = e->ref_reload[1] = 1;
    }
    for (int k = 0; k < 2; k++) {
        if (e->ref_reload[k]) {
            e->ref_x[k] = (int32_t) (reg32(e, 0x28 + 0x10 * (uint32_t) k) << 4) >> 4;
            e->ref_y[k] = (int32_t) (reg32(e, 0x2c + 0x10 * (uint32_t) k) << 4) >> 4;
            e->ref_reload[k] = 0;
        }
    }
    draw_bgs(e, y, dispcnt);
    draw_objs(e, y, dispcnt);
    build_window(e, y, dispcnt);
    compose(e, dispcnt);
    finish_line(e, y, dispcnt);
}

/* The main engine's finished lines: 256 x 192 pixels of red | green << 6 | blue << 12. */
const uint32_t *P(main_frame)(void)
{
    return engines[0].frame;
}
