/* scene_tween.h */
#include "scene_tween.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* How far a corner may have moved between the two scenes and still be the same polygon's: in
 * the screen's own measure (-1 to 1 across it), and in depth as a part of its distance. A
 * run scrolls the view by up to 0.08 a scene near the eye; the next tree of a row is 0.12 or
 * more away. */
#define MOVE_MOST 0.10f
#define DEPTH_MOST 0.20f
/* A polygon that follows the one before it in both scenes' order may have gone farther, when
 * it went the way that one went (the same thing, moving fast). */
#define FOLLOW_MOST 0.40f
#define FOLLOW_APART 0.04f
/* How far (in texels) a texture coordinate may have gone and still be put on the way: a
 * texture that flows (water) goes a texel or so a scene, and starts again a texture's width
 * back now and then. */
#define FLOW_MOST 8.0f
/* How many polygons alike are looked at for one polygon, at most, and how many polygons on
 * from each the two scenes' orders are compared. */
#define ALIKE_MOST 256
#define RUN_MOST 12

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

/* The way a polygon's first corner went, in the screen's measure. */
static void way(const SceneRaw *a, const SceneRaw *b, float out[2])
{
    const float *p = a->v[0].pos, *q = b->v[0].pos;
    float w = fabsf(p[3]) > fabsf(q[3]) ? fabsf(p[3]) : fabsf(q[3]);
    if (w < 1.0f / 64.0f) {
        w = 1.0f / 64.0f;
    }
    out[0] = (q[0] - p[0]) / w;
    out[1] = (q[1] - p[1]) / w;
}

/* Of the same kind (attributes, texture, number of corners). */
static int alike(const SceneRaw *a, const SceneRaw *b)
{
    return a->kind == b->kind && a->count == b->count;
}

/* The same piece of the texture too, or that piece moved as a whole (a texture that flows):
 * what a polygon stays from scene to scene, and another polygon of its kind is not. */
static int same(const SceneRaw *a, const SceneRaw *b)
{
    if (!alike(a, b)) {
        return 0;
    }
    if (a->sig == b->sig) {
        return 1;
    }
    float flow[2] = {a->v[0].st[0] - b->v[0].st[0], a->v[0].st[1] - b->v[0].st[1]};
    for (uint32_t k = 1; k < b->count; k++) {
        if (fabsf(a->v[k].st[0] - b->v[k].st[0] - flow[0]) >= 0.01f || fabsf(a->v[k].st[1] - b->v[k].st[1] - flow[1]) >= 0.01f) {
            return 0;
        }
    }
    return 1;
}

/* How well the two scenes' orders agree from these two polygons on: a point for each that is
 * of the same kind, and one more when its texture coordinates are the same too. */
static int run(const Scene *earlier, int k, const Scene *later, int j)
{
    int points = 0;
    for (int n = 0; n < RUN_MOST && k + n < earlier->raw_count && j + n < later->raw_count; n++) {
        const SceneRaw *a = &earlier->raws[k + n], *b = &later->raws[j + n];
        if (!alike(a, b)) {
            break;
        }
        points += 1 + (a->sig == b->sig);
    }
    return points;
}

/* How many times a polygon (by what does not move of it) is in the earlier scene and in the
 * later one (3: more than twice), in a table. */
typedef struct Seen {
    uint32_t sig;
    uint8_t used, times[2];
} Seen;

static Seen *seen_at(Seen *seen, int slots, uint32_t sig)
{
    uint32_t at = (sig * 2654435761u) & (uint32_t) (slots - 1);
    while (seen[at].used && seen[at].sig != sig) {
        at = (at + 1) & (uint32_t) (slots - 1);
    }
    seen[at].used = 1;
    seen[at].sig = sig;
    return &seen[at];
}

static unsigned long counts_so_far[5];

void scene_between_counts(unsigned long counts[5])
{
    memcpy(counts, counts_so_far, sizeof(counts_so_far));
}

/* Which polygon of the earlier scene each polygon given in the later one was: -1 for none
 * (-2: there were some alike, none of them it). The game gives a thing's polygons in the same
 * order every time, and the things in much the same order, so the two scenes are gone
 * through side by side: a polygon is the one after the last one found, if that is of its
 * kind and near where this one is. Where the orders part (a thing came into view or left
 * it), the place to go on from is looked for among the polygons of this kind that are near:
 * the one from which the two orders agree longest, and of those the nearest.
 *
 * Being nearest alone does not tell a thing's polygons apart: an animal's are a few dots from
 * one another and many have the same piece of the texture, so that with the view moving, a
 * neighbour is as near to where a polygon was as the polygon itself. */
static int *matches(const Scene *earlier, const Scene *later)
{
    int na = earlier->raw_count, nb = later->raw_count;
    int buckets = 1;
    while (buckets < 2 * na + 1) {
        buckets <<= 1;
    }
    int slots = 1;
    while (slots < 2 * (na + nb) + 1) {
        slots <<= 1;
    }
    int *match = malloc(sizeof(int) * (size_t) (nb + 1));
    int *head = malloc(sizeof(int) * (size_t) buckets), *next = malloc(sizeof(int) * (size_t) (na + 1));
    Seen *seen = calloc((size_t) slots, sizeof(Seen));
    if (match == NULL || head == NULL || next == NULL || seen == NULL) {
        free(match);
        free(head);
        free(next);
        free(seen);
        return NULL;
    }
    /* how often each polygon (what does not move of it) is in each scene: one that is there
       once in both is itself wherever it went */
    for (int i = 0; i < na + nb; i++) {
        Seen *at = seen_at(seen, slots, i < na ? earlier->raws[i].sig : later->raws[i - na].sig);
        at->times[i >= na] += at->times[i >= na] < 3;
    }
    /* the earlier scene's polygons by their kind: chains in a table, each in the scene's order */
    for (int i = 0; i < buckets; i++) {
        head[i] = -1;
    }
    for (int i = na - 1; i >= 0; i--) {
        uint32_t at = (earlier->raws[i].kind * 2654435761u) & (uint32_t) (buckets - 1);
        next[i] = head[at];
        head[at] = i;
    }
    int expect = 0;
    float went[2] = {0.0f, 0.0f};
    int went_known = 0;
    for (int j = 0; j < nb; j++) {
        const SceneRaw *b = &later->raws[j];
        int found = -1;
        /* the one after the last one found */
        if (expect < na && same(&earlier->raws[expect], b)) {
            const SceneRaw *a = &earlier->raws[expect];
            float d = apart(a, b);
            const Seen *at = seen_at(seen, slots, b->sig);
            if (d <= MOVE_MOST || (d <= FOLLOW_MOST && a->sig == b->sig && at->times[0] == 1 && at->times[1] == 1)) {
                found = expect;
            } else if (d <= FOLLOW_MOST && went_known) {
                float w[2];
                way(a, b, w);
                if (fabsf(w[0] - went[0]) <= FOLLOW_APART && fabsf(w[1] - went[1]) <= FOLLOW_APART) {
                    found = expect;
                }
            }
        }
        if (found < 0) {
            /* the orders part here: where to go on from */
            int best = 0, looked = 0;
            float nearest = 1e9f;
            const Seen *at = seen_at(seen, slots, b->sig);
            int once = at->times[0] == 1 && at->times[1] == 1;
            for (int k = head[(b->kind * 2654435761u) & (uint32_t) (buckets - 1)]; k >= 0 && looked < ALIKE_MOST; k = next[k]) {
                const SceneRaw *c = &earlier->raws[k];
                if (!same(c, b)) {
                    continue;
                }
                looked++;
                float d = apart(c, b);
                if (once && c->sig == b->sig && d <= FOLLOW_MOST) {
                    found = k; /* the only one there is */
                    break;
                }
                if (d > MOVE_MOST) {
                    continue;
                }
                /* of those from which the orders agree longest, the one that went most like
                   the last polygon found (things move with the view), or the nearest */
                int points = run(earlier, k, later, j);
                float off = d;
                if (went_known) {
                    float w[2];
                    way(c, b, w);
                    off = fabsf(w[0] - went[0]) > fabsf(w[1] - went[1]) ? fabsf(w[0] - went[0]) : fabsf(w[1] - went[1]);
                }
                if (points > best || (points == best && off < nearest)) {
                    best = points;
                    nearest = off;
                    found = k;
                }
            }
            if (found < 0 && looked > 0) {
                found = -2;
            }
        }
        match[j] = found;
        if (found >= 0) {
            way(&earlier->raws[found], b, went);
            went_known = 1;
            expect = found + 1;
        }
    }
    free(head);
    free(next);
    free(seen);
    return match;
}

/* The matches of the last two scenes asked about: a pair of scenes is asked about once for
 * each frame between them. */
static struct {
    const Scene *earlier, *later;
    uint32_t earlier_frame, later_frame;
    int earlier_raws, later_raws;
    int *match;
} kept;

static const int *matches_of(const Scene *earlier, const Scene *later)
{
    if (kept.match != NULL && kept.earlier == earlier && kept.later == later && kept.earlier_frame == earlier->frame &&
        kept.later_frame == later->frame && kept.earlier_raws == earlier->raw_count && kept.later_raws == later->raw_count) {
        return kept.match;
    }
    free(kept.match);
    kept.match = matches(earlier, later);
    kept.earlier = earlier;
    kept.later = later;
    kept.earlier_frame = earlier->frame;
    kept.later_frame = later->frame;
    kept.earlier_raws = earlier->raw_count;
    kept.later_raws = later->raw_count;
    return kept.match;
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
    const int *match = matches_of(earlier, later);
    if (match == NULL) {
        scene_free(scene);
        return NULL;
    }
    int count = 0, half_way = 0;
    for (int i = 0; i < later->polygon_count; i++) {
        const ScenePolygon *p = &later->polygons[i];
        ScenePolygon *q = &polygons[i];
        *q = *p;
        q->first = (uint32_t) count;
        const SceneRaw *b = p->raw >= 0 && p->raw < later->raw_count ? &later->raws[p->raw] : NULL, *a = NULL;
        if (b != NULL && match[p->raw] >= 0) {
            a = &earlier->raws[match[p->raw]];
        }
        counts_so_far[a != NULL ? a->sig != b->sig : b == NULL ? 2 : match[p->raw] == -1 ? 3 : 4]++;
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

int scene_between_same(const Scene *earlier, const Scene *later, int *checked)
{
    /* a check: when both scenes give the same polygons in the same order, each polygon's own
       place in the order is the right one */
    *checked = 0;
    if (earlier->raw_count != later->raw_count) {
        return 0;
    }
    for (int r = 0; r < later->raw_count; r++) {
        if (earlier->raws[r].sig != later->raws[r].sig) {
            return 0;
        }
    }
    const int *match = matches_of(earlier, later);
    int wrong = 0;
    for (int r = 0; match != NULL && r < later->raw_count; r++) {
        if (match[r] >= 0) {
            (*checked)++;
            wrong += match[r] != r;
        }
    }
    return wrong;
}
