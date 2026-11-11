/* scene_tween.h */
#include "scene_tween.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* How far a corner may have moved between the two scenes and still be the same polygon's: in
 * the screen's own measure (-1 to 1 across it), and in depth as a part of its distance. A
 * walk scrolls the view by about 0.02 in a thirtieth of a second; the next tree of a row is
 * 0.12 or more away. */
#define MOVE_MOST 0.10f
#define DEPTH_MOST 0.20f
/* How far (in texels) a texture coordinate may have gone and still be put on the way: a
 * texture that flows (water) goes a texel or so a scene, and starts again a texture's width
 * back now and then. */
#define FLOW_MOST 8.0f
/* How many polygons alike are looked at for one polygon, at most. */
#define ALIKE_MOST 512

/* The distance between a polygon as two scenes have it, or a large number when it is too far
 * to be the same one. */
static float apart(const SceneRaw *a, const SceneRaw *b)
{
    float most = 0.0f;
    for (uint32_t i = 0; i < b->count; i++) {
        const float *p = a->v[i].pos, *q = b->v[i].pos;
        float w = fabsf(p[3]) > fabsf(q[3]) ? fabsf(p[3]) : fabsf(q[3]);
        if (w < 1.0f / 64.0f) {
            w = 1.0f / 64.0f; /* at the eye: by the clip coordinates themselves */
        }
        float dx = fabsf(p[0] - q[0]) / w, dy = fabsf(p[1] - q[1]) / w, dw = fabsf(p[3] - q[3]) / w;
        if (dw > DEPTH_MOST) {
            return 1e9f;
        }
        most = dx > most ? dx : most;
        most = dy > most ? dy : most;
    }
    return most;
}

static unsigned long counts_so_far[5];

void scene_between_counts(unsigned long counts[5])
{
    memcpy(counts, counts_so_far, sizeof(counts_so_far));
}

Scene *scene_between(const Scene *earlier, Scene *later, float t, int *moved, int *of)
{
    const float s = 1.0f - t;
    ScenePolygon *polygons;
    SceneVertex *vertices;
    /* room: a polygon put on the way has its 3 or 4 corners, another keeps its own */
    Scene *scene = scene_like(later, later->polygon_count, later->vertex_count + 4 * later->polygon_count, &polygons, &vertices);
    if (scene == NULL) {
        return NULL;
    }
    /* the earlier scene's polygons by what they are: chains of those alike, in a table */
    int buckets = 1;
    while (buckets < 2 * earlier->raw_count + 1) {
        buckets <<= 1;
    }
    /* two tables: by all that does not move, and by the same without the texture coordinates */
    int *head = malloc(sizeof(int) * (size_t) buckets * 2), *next = malloc(sizeof(int) * (size_t) (earlier->raw_count + 1) * 2);
    if (head == NULL || next == NULL) {
        free(head);
        free(next);
        scene_free(scene);
        return NULL;
    }
    int *head_kind = head + buckets, *next_kind = next + earlier->raw_count + 1;
    for (int i = 0; i < 2 * buckets; i++) {
        head[i] = -1;
    }
    for (int i = earlier->raw_count - 1; i >= 0; i--) {
        uint32_t at = (earlier->raws[i].sig * 2654435761u) & (uint32_t) (buckets - 1);
        next[i] = head[at];
        head[at] = i;
        at = (earlier->raws[i].kind * 2654435761u) & (uint32_t) (buckets - 1);
        next_kind[i] = head_kind[at];
        head_kind[at] = i;
    }
    int count = 0, half_way = 0;
    for (int i = 0; i < later->polygon_count; i++) {
        const ScenePolygon *p = &later->polygons[i];
        ScenePolygon *q = &polygons[i];
        *q = *p;
        q->first = (uint32_t) count;
        const SceneRaw *b = p->raw >= 0 && p->raw < later->raw_count ? &later->raws[p->raw] : NULL, *a = NULL;
        int alike = 0, flowed = 0, looked = 0;
        if (b != NULL) {
            float nearest = MOVE_MOST;
            for (int k = head[(b->sig * 2654435761u) & (uint32_t) (buckets - 1)]; k >= 0 && looked < ALIKE_MOST; k = next[k]) {
                const SceneRaw *c = &earlier->raws[k];
                if (c->sig == b->sig && c->count == b->count) {
                    float d = apart(c, b);
                    looked++;
                    if (d <= nearest) {
                        nearest = d;
                        a = c;
                    }
                }
            }
            /* not found with its texture coordinates: one whose texture has flowed since */
            alike = looked;
            looked = 0;
            for (int k = a == NULL ? head_kind[(b->kind * 2654435761u) & (uint32_t) (buckets - 1)] : -1; k >= 0 && looked < ALIKE_MOST;
                 k = next_kind[k]) {
                const SceneRaw *c = &earlier->raws[k];
                if (c->kind == b->kind && c->count == b->count) {
                    float d = apart(c, b);
                    looked++;
                    if (d <= nearest) {
                        nearest = d;
                        a = c;
                        flowed = 1;
                    }
                }
            }
        }
        counts_so_far[a != NULL ? flowed : b == NULL ? 2 : alike + looked == 0 ? 3 : 4]++;
        if (a == NULL) {
            memcpy(vertices + count, later->vertices + p->first, sizeof(SceneVertex) * p->count);
            count += (int) p->count;
            continue;
        }
        q->count = b->count;
        for (uint32_t k = 0; k < b->count; k++) {
            SceneVertex *v = &vertices[count++];
            for (int c = 0; c < 4; c++) {
                v->pos[c] = s * a->v[k].pos[c] + t * b->v[k].pos[c];
            }
            for (int c = 0; c < 3; c++) {
                v->color[c] = s * a->v[k].color[c] + t * b->v[k].color[c];
            }
            v->st[0] = b->v[k].st[0];
            v->st[1] = b->v[k].st[1];
        }
        /* a texture that flowed a little: on its way too, the whole polygon by the same step */
        float flow[2] = {a->v[0].st[0] - b->v[0].st[0], a->v[0].st[1] - b->v[0].st[1]};
        int together = fabsf(flow[0]) <= FLOW_MOST && fabsf(flow[1]) <= FLOW_MOST;
        for (uint32_t k = 1; k < b->count && together; k++) {
            together = fabsf(a->v[k].st[0] - b->v[k].st[0] - flow[0]) < 0.01f && fabsf(a->v[k].st[1] - b->v[k].st[1] - flow[1]) < 0.01f;
        }
        for (uint32_t k = 0; k < b->count && together; k++) {
            vertices[count - (int) b->count + (int) k].st[0] += s * flow[0];
            vertices[count - (int) b->count + (int) k].st[1] += s * flow[1];
        }
        half_way++;
    }
    free(head);
    free(next);
    scene->polygon_count = later->polygon_count;
    scene->vertex_count = count;
    if (moved != NULL) {
        *moved = half_way;
    }
    if (of != NULL) {
        *of = later->polygon_count;
    }
    return scene;
}
