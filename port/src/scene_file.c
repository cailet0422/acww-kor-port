/* Scenes (scene.h) copied and kept in files. The file is the Scene structure's numbers, then
 * the polygons, the vertices, and each texture's record and texels, as this machine holds
 * them: a tool's format, not one to keep. */
#include "scene_file.h"

#include <stdlib.h>
#include <string.h>

#define MAGIC 0x334e4353u /* "SCN3" */

Scene *scene_copy(const Scene *scene)
{
    Scene *copy = malloc(sizeof(*copy));
    if (copy == NULL) {
        return NULL;
    }
    *copy = *scene;
    ScenePolygon *polygons = malloc(sizeof(ScenePolygon) * (size_t) (scene->polygon_count + 1));
    SceneVertex *vertices = malloc(sizeof(SceneVertex) * (size_t) (scene->vertex_count + 1));
    SceneTexture *textures = calloc((size_t) scene->texture_count + 1, sizeof(SceneTexture));
    SceneRaw *raws = malloc(sizeof(SceneRaw) * (size_t) (scene->raw_count + 1));
    copy->polygons = polygons;
    copy->vertices = vertices;
    copy->textures = textures;
    copy->raws = raws;
    copy->holders = malloc(sizeof(int));
    copy->lender = NULL;
    if (polygons == NULL || vertices == NULL || textures == NULL || raws == NULL || copy->holders == NULL) {
        copy->texture_count = 0;
        if (copy->holders != NULL) {
            *copy->holders = 1;
        }
        scene_free(copy);
        return NULL;
    }
    *copy->holders = 1;
    memcpy(polygons, scene->polygons, sizeof(ScenePolygon) * (size_t) scene->polygon_count);
    memcpy(vertices, scene->vertices, sizeof(SceneVertex) * (size_t) scene->vertex_count);
    if (scene->raw_count > 0) {
        memcpy(raws, scene->raws, sizeof(SceneRaw) * (size_t) scene->raw_count);
    }
    for (int i = 0; i < scene->texture_count; i++) {
        size_t bytes = (size_t) scene->textures[i].width * (size_t) scene->textures[i].height * 4;
        uint32_t *rgba = malloc(bytes);
        textures[i] = scene->textures[i];
        textures[i].rgba = rgba;
        if (rgba == NULL) {
            copy->texture_count = i;
            scene_free(copy);
            return NULL;
        }
        memcpy(rgba, scene->textures[i].rgba, bytes);
    }
    return copy;
}

Scene *scene_hold(Scene *scene)
{
    if (scene != NULL && scene->holders != NULL) {
        __atomic_add_fetch(scene->holders, 1, __ATOMIC_SEQ_CST);
    }
    return scene;
}

Scene *scene_like(Scene *like, int polygons, int vertices, ScenePolygon **polygon_room, SceneVertex **vertex_room)
{
    Scene *scene = malloc(sizeof(*scene));
    ScenePolygon *p = malloc(sizeof(ScenePolygon) * (size_t) (polygons + 1));
    SceneVertex *v = malloc(sizeof(SceneVertex) * (size_t) (vertices + 1));
    int *holders = malloc(sizeof(int));
    if (scene == NULL || p == NULL || v == NULL || holders == NULL) {
        free(scene);
        free(p);
        free(v);
        free(holders);
        return NULL;
    }
    *scene = *like;
    *holders = 1;
    scene->holders = holders;
    scene->lender = scene_hold(like); /* its textures are this one's while it lives */
    scene->polygons = p;
    scene->vertices = v;
    scene->polygon_count = scene->vertex_count = 0;
    scene->raws = NULL;
    scene->raw_count = 0;
    *polygon_room = p;
    *vertex_room = v;
    return scene;
}

void scene_free(Scene *scene)
{
    if (scene == NULL) {
        return;
    }
    if (scene->holders != NULL && __atomic_sub_fetch(scene->holders, 1, __ATOMIC_SEQ_CST) > 0) {
        return; /* still held */
    }
    if (scene->lender != NULL) {
        scene_free(scene->lender);
    } else {
        for (int i = 0; i < scene->texture_count; i++) {
            free((void *) scene->textures[i].rgba);
        }
        free((void *) scene->textures);
    }
    free((void *) scene->polygons);
    free((void *) scene->vertices);
    free((void *) scene->raws);
    free(scene->holders);
    free(scene);
}

int scene_write(const char *path, const Scene *scene)
{
    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        return -1;
    }
    uint32_t magic = MAGIC;
    Scene head = *scene; /* (the raws are written after the textures; no pointers of this run) */
    head.holders = NULL;
    head.lender = NULL;
    int ok = fwrite(&magic, 4, 1, f) == 1 && fwrite(&head, sizeof(head), 1, f) == 1;
    ok = ok && fwrite(scene->polygons, sizeof(ScenePolygon), (size_t) scene->polygon_count, f) == (size_t) scene->polygon_count;
    ok = ok && fwrite(scene->vertices, sizeof(SceneVertex), (size_t) scene->vertex_count, f) == (size_t) scene->vertex_count;
    for (int i = 0; ok && i < scene->texture_count; i++) {
        size_t texels = (size_t) scene->textures[i].width * (size_t) scene->textures[i].height;
        ok = fwrite(&scene->textures[i], sizeof(SceneTexture), 1, f) == 1 && fwrite(scene->textures[i].rgba, 4, texels, f) == texels;
    }
    ok = ok && fwrite(scene->raws, sizeof(SceneRaw), (size_t) scene->raw_count, f) == (size_t) scene->raw_count;
    return fclose(f) == 0 && ok ? 0 : -1;
}

Scene *scene_read(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return NULL;
    }
    uint32_t magic = 0;
    Scene head;
    if (fread(&magic, 4, 1, f) != 1 || magic != MAGIC || fread(&head, sizeof(head), 1, f) != 1 || head.polygon_count < 0 ||
        head.polygon_count > 1 << 16 || head.vertex_count < 0 || head.vertex_count > 1 << 20 || head.texture_count < 0 ||
        head.texture_count > 4096 || head.raw_count < 0 || head.raw_count > 1 << 16) {
        fclose(f);
        return NULL;
    }
    Scene *scene = malloc(sizeof(*scene));
    ScenePolygon *polygons = malloc(sizeof(ScenePolygon) * (size_t) (head.polygon_count + 1));
    SceneVertex *vertices = malloc(sizeof(SceneVertex) * (size_t) (head.vertex_count + 1));
    SceneTexture *textures = calloc((size_t) head.texture_count + 1, sizeof(SceneTexture));
    SceneRaw *raws = malloc(sizeof(SceneRaw) * (size_t) (head.raw_count + 1));
    int *holders = malloc(sizeof(int));
    if (scene == NULL || polygons == NULL || vertices == NULL || textures == NULL || raws == NULL || holders == NULL) {
        free(scene);
        free(polygons);
        free(vertices);
        free(textures);
        free(raws);
        free(holders);
        fclose(f);
        return NULL;
    }
    *scene = head;
    *holders = 1;
    scene->holders = holders;
    scene->lender = NULL;
    scene->polygons = polygons;
    scene->vertices = vertices;
    scene->textures = textures;
    scene->raws = raws;
    scene->texture_count = 0;
    int ok = fread(polygons, sizeof(ScenePolygon), (size_t) head.polygon_count, f) == (size_t) head.polygon_count &&
             fread(vertices, sizeof(SceneVertex), (size_t) head.vertex_count, f) == (size_t) head.vertex_count;
    for (int i = 0; ok && i < head.texture_count; i++) {
        ok = fread(&textures[i], sizeof(SceneTexture), 1, f) == 1 && textures[i].width > 0 && textures[i].width <= 1024 &&
             textures[i].height > 0 && textures[i].height <= 1024;
        textures[i].rgba = NULL;
        if (ok) {
            size_t texels = (size_t) textures[i].width * (size_t) textures[i].height;
            uint32_t *rgba = malloc(texels * 4);
            textures[i].rgba = rgba;
            scene->texture_count = i + 1;
            ok = rgba != NULL && fread(rgba, 4, texels, f) == texels;
        }
    }
    ok = ok && fread(raws, sizeof(SceneRaw), (size_t) head.raw_count, f) == (size_t) head.raw_count;
    fclose(f);
    for (int i = 0; ok && i < head.polygon_count; i++) {
        ok = polygons[i].raw < head.raw_count && polygons[i].first + polygons[i].count <= (uint32_t) head.vertex_count;
    }
    if (!ok) {
        scene_free(scene);
        return NULL;
    }
    return scene;
}
