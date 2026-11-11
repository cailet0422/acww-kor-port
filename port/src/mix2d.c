#include "mix2d.h"

#include <stddef.h>

#include "gpu2d.h"

/* the game's own copy of the engines (gpu2d_main.c) and the three with flat 3D layers
 * (gpu2d_probe.c) */
void gpu2d_main_init(void);
void gpu2d_main_line(uint32_t y);
void gpu2d_main_register_written(uint32_t offset, uint32_t value);

#define PROBE(n)                                                           \
    void gpu2d_probe##n##_init(void);                                      \
    void gpu2d_probe##n##_main_line(uint32_t y);                           \
    void gpu2d_probe##n##_register_written(uint32_t offset, uint32_t value); \
    const uint32_t *gpu2d_probe##n##_main_frame(void);
PROBE(0)
PROBE(1)
PROBE(2)
#undef PROBE

static int enabled;

void mix2d_enable(int on)
{
    enabled = on;
}

/* What a copy asks of the runtime ------------------------------------------------------------ */

/* The flat 3D layers: black and white with full alpha, and nothing. */
const uint32_t *mix2d_probe_layer(int probe, int y)
{
    static uint32_t lines[3][GPU2D_WIDTH];
    static int ready;
    (void) y;
    if (!ready) {
        for (int x = 0; x < GPU2D_WIDTH; x++) {
            lines[0][x] = 31u << 24;
            lines[1][x] = 31u << 24 | 0x3f3f3f;
            lines[2][x] = 0;
        }
        ready = 1;
    }
    return lines[probe];
}

/* never changes: the layers are made once */
uint32_t mix2d_probe_generation(void)
{
    return 1;
}

/* not reached: the copies never capture the display, output a frame or store a register */
const uint16_t *mix2d_probe_capture_line(int y)
{
    (void) y;
    return NULL;
}

void mix2d_probe_frame_output(uint32_t frame, const uint32_t *upper, const uint32_t *lower)
{
    (void) frame;
    (void) upper;
    (void) lower;
}

void mix2d_probe_reg_store(uint32_t offset, uint32_t value)
{
    (void) offset;
    (void) value;
}

/* The engines' entry points (runtime/gpu2d.h), in front of all the copies -------------------- */

void gpu2d_init(void)
{
    gpu2d_main_init();
    if (enabled) {
        gpu2d_probe0_init();
        gpu2d_probe1_init();
        gpu2d_probe2_init();
    }
}

void gpu2d_line(uint32_t y)
{
    gpu2d_main_line(y);
    if (enabled) {
        gpu2d_probe0_main_line(y);
        gpu2d_probe1_main_line(y);
        gpu2d_probe2_main_line(y);
    }
}

void gpu2d_register_written(uint32_t offset, uint32_t value)
{
    gpu2d_main_register_written(offset, value);
    if (enabled) {
        gpu2d_probe0_register_written(offset, value);
        gpu2d_probe1_register_written(offset, value);
        gpu2d_probe2_register_written(offset, value);
    }
}

int mix2d_answers(const uint32_t **black, const uint32_t **white, const uint32_t **none)
{
    if (!enabled) {
        return 0;
    }
    *black = gpu2d_probe0_main_frame();
    *white = gpu2d_probe1_main_frame();
    *none = gpu2d_probe2_main_frame();
    return 1;
}
