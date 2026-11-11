/* A copy of the 3D engine as tall3d.c uses it (gpu3d_tile.c makes them). */
#ifndef ACWW_PORT_TALL3D_ENGINE_H
#define ACWW_PORT_TALL3D_ENGINE_H

#include <stdint.h>

typedef struct Engine3d {
    void (*init)(void);
    void (*write)(uint32_t offset, uint32_t value);
    void (*dot_depth_write)(uint32_t value);
    void (*gxstat_write)(uint32_t value);
    void (*vblank)(void);
    void (*render)(int list);
    const uint32_t *(*line_rgb6)(int y);
    int (*prepare)(void);
} Engine3d;

/* as many as a picture three times the DS's size needs: 3 x 6 tiles */
#define TALL3D_ENGINES 18
#define TALL3D_SCALE_MAX 3

#endif
