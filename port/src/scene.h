/* A frame's 3D scene as the DS's geometry engine leaves it, for a renderer that is not the
 * DS's own (docs/renderer.md): the polygons, transformed, lit, clipped and put in drawing
 * order, the textures they use, decoded, and the registers the drawing goes by.
 *
 * gpu3d_tile.c makes one out of a copy of the decomp's engine; the positions are clip
 * coordinates, so the scene can be drawn at any size.
 */
#ifndef ACWW_PORT_SCENE_H
#define ACWW_PORT_SCENE_H

#include <stdint.h>

typedef struct SceneVertex {
    float pos[4];   /* clip coordinates as the DS has them: x, y and z from -w to w, y up */
    float color[3]; /* 0 to 1 */
    float st[2];    /* texture coordinates in texels */
} SceneVertex;

typedef struct ScenePolygon {
    uint32_t first, count;        /* its vertices: a convex polygon */
    uint32_t attr, texparam, pltt; /* POLYGON_ATTR, TEXIMAGE_PARAM, PLTT_BASE as the DS has them */
    int32_t texture;              /* into the scene's textures, or -1: not textured */
    uint8_t translucent;          /* drawn after the opaque ones, in order */
    uint8_t wbuffer;              /* depth is w, not z */
    uint8_t facing;               /* a front face */
    uint8_t shadow_mask, shadow;  /* a shadow volume's mask, and the shadow drawn where it marked */
} ScenePolygon;

typedef struct SceneTexture {
    uint32_t texparam, pltt; /* what it was decoded from (the bits that choose the texels) */
    uint32_t hash;           /* of its contents: the same key with another hash is a changed texture */
    int width, height;
    const uint32_t *rgba;    /* width x height, bytes R G B A, alpha 0 to 255; valid during the sink's call */
    uint8_t translucent;     /* it has texels with alpha between 0 and 31 of 31 */
} SceneTexture;

typedef struct Scene {
    uint32_t frame;          /* a count of the scenes made */
    int tall;                /* the view is two screens high (the upper half above the lower) */
    uint32_t disp3dcnt;      /* DISP3DCNT: bit 0 texturing, 1 highlight for toon, 2 alpha test, 3 blending,
                                4 anti-aliasing, 5 edge marking, 6 fog on alpha only, 7 fog, 8-11 its shift */
    uint32_t alpha_ref;      /* pixels with alpha at or below it (0 to 31) are not drawn */
    uint32_t clear_color;    /* CLEAR_COLOR: 0-14 the colour, 15 fog, 16-20 alpha, 24-29 the ID */
    uint32_t clear_depth;    /* CLEAR_DEPTH's 15 bits */
    uint32_t fog_color, fog_offset, fog_shift;
    uint8_t fog_table[32];
    uint16_t toon[32], edge[8];
    const ScenePolygon *polygons;
    int polygon_count;
    const SceneVertex *vertices;
    int vertex_count;
    const SceneTexture *textures;
    int texture_count;
} Scene;

/* Who takes the scenes: called on the scene engine's thread for each frame, with a scene that
 * is valid until it returns. */
typedef void (*SceneSink)(const Scene *scene, void *user);

#endif
