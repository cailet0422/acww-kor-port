/* The scenes made between two of the game's (src/scene_tween.c), on scenes made up here: no
 * ROM is needed.
 *
 *     acww-tween-test
 *
 * Each polygon given has to come out part of the way from where it was itself, whatever
 * other polygons alike are near it.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/scene_tween.h"

#define MOST 4096

typedef struct Made {
    SceneRaw raws[MOST];
    ScenePolygon polygons[MOST];
    SceneVertex vertices[MOST * 4];
    int own[MOST]; /* which thing's polygon each is: the same number in both scenes */
    int count;
} Made;

static int failures;

static void check(int ok, const char *what)
{
    if (!ok) {
        printf("FAILED: %s\n", what);
        failures++;
    }
}

/* A quad of `size` at (x, y), at depth w, of a kind and with its texture's piece at (s, t). */
static void quad(Made *m, int own, uint32_t kind, float x, float y, float w, float size, float s, float t)
{
    SceneRaw *r = &m->raws[m->count];
    const float corner[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    memset(r, 0, sizeof(*r));
    r->count = 4;
    r->kind = kind;
    r->sig = kind * 31u + (uint32_t) (s * 16.0f) * 7u + (uint32_t) (t * 16.0f) * 13u;
    for (int i = 0; i < 4; i++) {
        r->v[i].pos[0] = (x + corner[i][0] * size) * w;
        r->v[i].pos[1] = (y + corner[i][1] * size) * w;
        r->v[i].pos[2] = 0.0f;
        r->v[i].pos[3] = w;
        r->v[i].color[0] = r->v[i].color[1] = r->v[i].color[2] = 1.0f;
        r->v[i].st[0] = s + corner[i][0] * 8.0f;
        r->v[i].st[1] = t + corner[i][1] * 8.0f;
    }
    ScenePolygon *p = &m->polygons[m->count];
    memset(p, 0, sizeof(*p));
    p->first = (uint32_t) m->count * 4;
    p->count = 4;
    p->texture = -1;
    p->raw = m->count;
    memcpy(&m->vertices[m->count * 4], r->v, sizeof(r->v));
    m->own[m->count] = own;
    m->count++;
}

/* An animal: a block of small quads a few dots apart, most of them with the same piece of
 * the texture (a coat of one colour), at (x, y). Its polygons are numbered from `own`. */
static void animal(Made *m, int own, float x, float y, float sway)
{
    for (int i = 0; i < 80; i++) {
        float px = x + 0.012f * (float) (i % 8) + sway * (float) (i / 8) * 0.002f, py = y + 0.012f * (float) (i / 8);
        quad(m, own + i, 7, px, py, 1.0f, 0.012f, i % 10 == 9 ? 16.0f : 0.0f, 0.0f);
    }
}

/* A row of trees, each the same few polygons, `gap` apart. */
static void trees(Made *m, int own, int first, int n, float x, float gap)
{
    for (int k = first; k < first + n; k++) {
        for (int i = 0; i < 6; i++) {
            quad(m, own + k * 6 + i, 9, x + gap * (float) k + 0.02f * (float) (i % 2), 0.3f + 0.03f * (float) (i / 2), 1.0f, 0.03f,
                 8.0f * (float) i, 0.0f);
        }
    }
}

static void ground(Made *m, int own, float x, float y)
{
    for (int i = 0; i < 64; i++) {
        /* (each piece of ground has its own piece of the texture, as most of the game's do) */
        quad(m, own + i, 3, x + 0.25f * (float) (i % 8) - 1.0f, y + 0.25f * (float) (i / 8) - 1.0f, 1.0f, 0.25f, 8.0f * (float) i, 0.0f);
    }
}

static Scene *kept(const Made *m, uint32_t frame)
{
    Scene scene;
    memset(&scene, 0, sizeof(scene));
    scene.frame = frame;
    scene.polygons = m->polygons;
    scene.polygon_count = m->count;
    scene.vertices = m->vertices;
    scene.vertex_count = m->count * 4;
    scene.raws = m->raws;
    scene.raw_count = m->count;
    return scene_copy(&scene);
}

/* The scene between the two, a part `t` of the way: every polygon whose thing is in both has
 * to be that part of the way from where it was (those in `still` have to be where the later
 * scene has them: they are new, or the picture was cut). Returns how many were on the way. */
static int between(const char *what, const Made *a, const Made *b, float t, int still)
{
    Scene *earlier = kept(a, 1), *later = kept(b, 2);
    int moved = 0, of = 0, wrong = 0, on_the_way = 0;
    Scene *made = scene_between(earlier, later, t, &moved, &of);
    check(made != NULL && of == b->count, what);
    for (int j = 0; made != NULL && j < b->count; j++) {
        int was = -1;
        for (int i = 0; i < a->count && was < 0; i++) {
            was = a->own[i] == b->own[j] ? i : -1;
        }
        const SceneVertex *v = &made->vertices[made->polygons[j].first];
        for (int k = 0; k < 4; k++) {
            const float *q = b->raws[j].v[k].pos;
            float want[2] = {q[0], q[1]};
            if (was >= 0 && !still) {
                const float *p = a->raws[was].v[k].pos;
                want[0] = (1.0f - t) * p[0] + t * q[0];
                want[1] = (1.0f - t) * p[1] + t * q[1];
            }
            if (fabsf(v[k].pos[0] - want[0]) > 1e-4f || fabsf(v[k].pos[1] - want[1]) > 1e-4f) {
                if (wrong < 8) {
                    printf("  polygon %d (thing %d, %s): at %.3f %.3f, belongs at %.3f %.3f\n", j, b->own[j], was >= 0 ? "in both" : "new",
                           v[k].pos[0], v[k].pos[1], want[0], want[1]);
                }
                wrong++;
                break;
            }
        }
        on_the_way += was >= 0 && !still;
    }
    if (wrong != 0) {
        printf("FAILED: %s: %d of %d polygons are not where they belong\n", what, wrong, b->count);
        failures++;
    } else {
        printf("ok: %s (%d of %d polygons on their way)\n", what, moved, of);
    }
    check(still ? moved == 0 : moved == on_the_way, what);
    scene_free(made);
    scene_free(earlier);
    scene_free(later);
    return moved;
}

int main(void)
{
    static Made a, b;

    /* an animal walking while the view scrolls the other way: every polygon goes about as
       far as its neighbours are apart */
    a.count = b.count = 0;
    ground(&a, 1000, 0.0f, 0.0f);
    animal(&a, 0, 0.10f, 0.10f, 0.0f);
    ground(&b, 1000, -0.03f, 0.02f);
    animal(&b, 0, 0.10f - 0.05f, 0.10f + 0.03f, 1.0f);
    between("an animal crossing the view", &a, &b, 1.0f / 3.0f, 0);
    between("the same, two thirds of the way", &a, &b, 2.0f / 3.0f, 0);

    /* a run: the view goes more than a tenth of the screen in one scene */
    a.count = b.count = 0;
    ground(&a, 1000, 0.0f, 0.0f);
    animal(&a, 0, 0.10f, 0.10f, 0.0f);
    trees(&a, 2000, 0, 6, -0.6f, 0.2f);
    ground(&b, 1000, -0.16f, 0.0f);
    animal(&b, 0, 0.10f - 0.16f, 0.10f, 0.0f);
    trees(&b, 2000, 0, 6, -0.6f - 0.16f, 0.2f);
    between("a fast run past trees and an animal", &a, &b, 0.5f, 0);

    /* a tree comes into view at one end of the row and one leaves at the other; a second
       animal appears before the first in the order */
    a.count = b.count = 0;
    ground(&a, 1000, 0.0f, 0.0f);
    trees(&a, 2000, 0, 6, -0.6f, 0.2f);
    animal(&a, 0, 0.10f, 0.10f, 0.0f);
    ground(&b, 1000, 0.04f, 0.0f);
    trees(&b, 2000, -1, 6, -0.6f + 0.04f, 0.2f);
    animal(&b, 500, -0.50f, -0.40f, 0.0f);
    animal(&b, 0, 0.10f + 0.04f, 0.10f, 0.0f);
    between("things coming into view and leaving it", &a, &b, 0.5f, 0);

    /* water: the texture's piece is another one in every scene */
    a.count = b.count = 0;
    for (int i = 0; i < 40; i++) {
        quad(&a, i, 5, 0.1f * (float) (i % 8) - 0.4f, 0.1f * (float) (i / 8), 1.0f, 0.1f, 4.0f * (float) (i % 3), 0.0f);
        quad(&b, i, 5, 0.1f * (float) (i % 8) - 0.4f + 0.02f, 0.1f * (float) (i / 8), 1.0f, 0.1f, 4.0f * (float) (i % 3) + 1.5f, 0.0f);
    }
    {
        Scene *earlier = kept(&a, 1), *later = kept(&b, 2);
        Scene *made = scene_between(earlier, later, 0.5f, NULL, NULL);
        check(made != NULL && fabsf(made->vertices[0].st[0] - 0.75f) < 1e-3f, "water's texture half way");
        scene_free(made);
        scene_free(earlier);
        scene_free(later);
    }
    between("water, its texture flowing", &a, &b, 0.5f, 0);

    /* a cut to another view: nothing is where it was, and nothing is moved */
    a.count = b.count = 0;
    ground(&a, 1000, 0.0f, 0.0f);
    animal(&a, 0, 0.10f, 0.10f, 0.0f);
    for (int i = 0; i < 64; i++) {
        quad(&b, 1000 + i, 3, 0.25f * (float) ((i * 5 + 3) % 8) - 1.0f, 0.25f * (float) ((i * 3 + 5) % 8) - 1.0f, 2.0f, 0.25f, 0.0f, 0.0f);
    }
    animal(&b, 0, -0.70f, -0.60f, 0.0f);
    between("a cut to another view", &a, &b, 0.5f, 1);

    printf(failures ? "tween-test: FAILED\n" : "tween-test: passed\n");
    return failures != 0;
}
