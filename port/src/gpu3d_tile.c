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

#if TILE == 0
/* The scene, for another renderer (scene.h): only the first copy has this ------------------- */

#include "scene.h"

#define SCENE_TEXTURES 256

static ScenePolygon scene_polygons[POLYGON_RAM];
static SceneVertex scene_vertices[POLYGON_RAM * MAX_POLY_VERTICES];
static SceneTexture scene_textures[SCENE_TEXTURES];

/* Textures decoded before and still as they were: by what chose their texels, with a hash of
 * the bytes they were decoded from. */
static struct Decoded {
    uint32_t texparam, pltt, source_hash, hash, used;
    int width, height, translucent;
    uint32_t *rgba;
} decoded[SCENE_TEXTURES];
static uint32_t scene_count;

/* the bits of TEXIMAGE_PARAM that choose the texels: the address, the sizes, the format and
   whether colour 0 is transparent (not how the coordinates repeat or are made) */
#define TEXELS_OF 0x3ff0ffffu

static uint32_t hash_byte(uint32_t h, uint32_t b)
{
    return (h ^ b) * 16777619u;
}

/* A hash of what a texture is decoded from: its texels and its palette's entries. 0 for the
 * compressed format, whose palette is all over the slot: that one is decoded every time. */
static uint32_t source_hash(uint32_t param, uint32_t pltt)
{
    uint32_t format = (param >> 26) & 7, texels = (8u << ((param >> 20) & 7)) * (8u << ((param >> 23) & 7));
    uint32_t base = (param & 0xffff) << 3, bytes, colors, pal = format == 2 ? pltt << 3 : pltt << 4, h = 2166136261u;
    switch (format) {
        case 1: bytes = texels, colors = 32; break;
        case 2: bytes = texels / 4, colors = 4; break;
        case 3: bytes = texels / 2, colors = 16; break;
        case 4: bytes = texels, colors = 256; break;
        case 6: bytes = texels, colors = 8; break;
        case 7: bytes = texels * 2, colors = 0; break;
        default: return 0;
    }
    for (uint32_t i = 0; i < bytes; i++) {
        h = hash_byte(h, tex_read8(base + i));
    }
    for (uint32_t i = 0; i < colors; i++) {
        uint32_t c = pal_read16(pal + 2 * i);
        h = hash_byte(hash_byte(h, c & 255), c >> 8);
    }
    return h ? h : 1;
}

/* The texture of a polygon, decoded (or found as decoded before); its place in decoded[]. */
static int texture_decoded(uint32_t texparam, uint32_t pltt)
{
    uint32_t key = texparam & TEXELS_OF, source = source_hash(key, pltt);
    int at = -1, oldest = 0;
    for (int i = 0; i < SCENE_TEXTURES; i++) {
        if (decoded[i].rgba != NULL && decoded[i].texparam == key && decoded[i].pltt == pltt) {
            at = i;
            break;
        }
        if (decoded[i].used < decoded[oldest].used) {
            oldest = i;
        }
    }
    if (at >= 0 && source != 0 && decoded[at].source_hash == source) {
        decoded[at].used = scene_count;
        return at;
    }
    if (at >= 0 && decoded[at].used == scene_count) {
        return at; /* decoded for this very scene (the compressed format) */
    }
    at = at >= 0 ? at : oldest;
    struct Decoded *d = &decoded[at];
    int width = 8 << ((key >> 20) & 7), height = 8 << ((key >> 23) & 7);
    if (d->rgba == NULL || d->width * d->height != width * height) {
        free(d->rgba);
        d->rgba = malloc((size_t) width * (size_t) height * 4);
    }
    if (d->rgba == NULL) {
        return -1;
    }
    uint32_t h = 2166136261u;
    int translucent = 0;
    /* each texel once, read as the engine's own rasteriser reads it (clamped coordinates) */
    uint32_t plain = key & ~0xf0000u;
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            uint32_t color, alpha;
            sample_texture(plain, pltt, x << 4, y << 4, &color, &alpha);
            uint32_t c = rgb555_to_6(color), r = (c & 63) * 255 / 63, g = ((c >> 8) & 63) * 255 / 63, b = ((c >> 16) & 63) * 255 / 63;
            uint32_t a = alpha * 255 / 31, texel = r | g << 8 | b << 16 | a << 24;
            translucent |= alpha != 0 && alpha != 31;
            d->rgba[y * width + x] = texel;
            h = (h ^ texel) * 16777619u;
        }
    }
    d->texparam = key;
    d->pltt = pltt;
    d->source_hash = source;
    d->hash = h;
    d->width = width;
    d->height = height;
    d->translucent = translucent;
    d->used = scene_count;
    return at;
}

/* The list of a swap as a scene, at the moment the engine would draw it (the registers and
 * the texture memory are read as its rasteriser reads them). */
static void T(scene)(int list_index, Scene *scene)
{
    PolygonList *list = &lists[list_index];
    static const Polygon *order[POLYGON_RAM];
    static int16_t texture_of[SCENE_TEXTURES];
    capture_registers();
    scene_count++;
    memset(scene, 0, sizeof(*scene));
    scene->frame = scene_count;
    scene->disp3dcnt = rr.disp3dcnt;
    scene->alpha_ref = rr.alpha_ref;
    scene->clear_color = rr.clear1;
    scene->clear_depth = rr.clear2 & 0x7fff;
    scene->fog_color = rr.fog_color;
    scene->fog_offset = rr.fog_offset;
    scene->fog_shift = rr.fog_shift;
    memcpy(scene->fog_table, rr.fog_table + 1, 32);
    memcpy(scene->toon, rr.toon, sizeof(rr.toon));
    memcpy(scene->edge, rr.edge, sizeof(rr.edge));
    for (int i = 0; i < SCENE_TEXTURES; i++) {
        texture_of[i] = -1;
    }
    for (int i = 0; i < list->polygons; i++) {
        order[i] = &list->poly[i];
    }
    qsort(order, (size_t) list->polygons, sizeof(order[0]), list->manual_sort ? compare_order : compare_polygons);
    int polygons = 0, vertices = 0, textures = 0;
    for (int i = 0; i < list->polygons; i++) {
        const Polygon *p = order[i];
        if (p->count < 3) {
            continue;
        }
        ScenePolygon *q = &scene_polygons[polygons++];
        q->first = (uint32_t) vertices;
        q->count = (uint32_t) p->count;
        q->attr = p->attr;
        q->texparam = p->texparam;
        q->pltt = p->pltt;
        q->translucent = p->translucent;
        q->wbuffer = p->wbuffer;
        q->facing = p->facing;
        q->shadow_mask = p->shadow_mask;
        q->shadow = p->shadow;
        q->texture = -1;
        if ((rr.disp3dcnt & 1) && ((p->texparam >> 26) & 7) != 0) {
            int at = texture_decoded(p->texparam, p->pltt);
            if (at >= 0) {
                if (texture_of[at] < 0) {
                    const struct Decoded *d = &decoded[at];
                    texture_of[at] = (int16_t) textures;
                    scene_textures[textures++] = (SceneTexture){d->texparam, d->pltt, d->hash, d->width, d->height, d->rgba,
                                                                (uint8_t) d->translucent};
                }
                q->texture = texture_of[at];
            }
        }
        for (int k = 0; k < p->count; k++) {
            const GVertex *v = &p->v[k];
            SceneVertex *o = &scene_vertices[vertices++];
            for (int c = 0; c < 4; c++) {
                o->pos[c] = (float) v->pos[c] / 4096.0f;
            }
            for (int c = 0; c < 3; c++) {
                o->color[c] = (float) ((v->rgb[c] >> 3) & 0x3f) / 63.0f;
            }
            o->st[0] = (float) v->tex[0] / 16.0f;
            o->st[1] = (float) v->tex[1] / 16.0f;
        }
    }
    scene->polygons = scene_polygons;
    scene->polygon_count = polygons;
    scene->vertices = scene_vertices;
    scene->vertex_count = vertices;
    scene->textures = scene_textures;
    scene->texture_count = textures;
}
#define SCENE_OF T(scene)
#else
#define SCENE_OF NULL
#endif

const Engine3d T(engine) = {
    T(init), T(write), T(dot_depth_write), T(gxstat_write), T(vblank), T(render), T(line_rgb6), T(prepare), SCENE_OF,
};
