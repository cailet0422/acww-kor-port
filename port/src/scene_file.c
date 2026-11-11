/* Scenes (scene.h) copied and kept in files. The file is the Scene structure's numbers, then
 * the polygons, the vertices, and each texture's record and texels, as this machine holds
 * them: a tool's format, not one to keep. */
#include "scene_file.h"

#include <stdlib.h>
#include <string.h>

#define MAGIC 0x314e4353u /* "SCN1" */

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
    copy->polygons = polygons;
    copy->vertices = vertices;
    copy->textures = textures;
    if (polygons == NULL || vertices == NULL || textures == NULL) {
        copy->texture_count = 0;
        scene_free(copy);
        return NULL;
    }
    memcpy(polygons, scene->polygons, sizeof(ScenePolygon) * (size_t) scene->polygon_count);
    memcpy(vertices, scene->vertices, sizeof(SceneVertex) * (size_t) scene->vertex_count);
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

void scene_free(Scene *scene)
{
    if (scene == NULL) {
        return;
    }
    for (int i = 0; i < scene->texture_count; i++) {
        free((void *) scene->textures[i].rgba);
    }
    free((void *) scene->polygons);
    free((void *) scene->vertices);
    free((void *) scene->textures);
    free(scene);
}

int scene_write(const char *path, const Scene *scene)
{
    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        return -1;
    }
    uint32_t magic = MAGIC;
    int ok = fwrite(&magic, 4, 1, f) == 1 && fwrite(scene, sizeof(*scene), 1, f) == 1;
    ok = ok && fwrite(scene->polygons, sizeof(ScenePolygon), (size_t) scene->polygon_count, f) == (size_t) scene->polygon_count;
    ok = ok && fwrite(scene->vertices, sizeof(SceneVertex), (size_t) scene->vertex_count, f) == (size_t) scene->vertex_count;
    for (int i = 0; ok && i < scene->texture_count; i++) {
        size_t texels = (size_t) scene->textures[i].width * (size_t) scene->textures[i].height;
        ok = fwrite(&scene->textures[i], sizeof(SceneTexture), 1, f) == 1 && fwrite(scene->textures[i].rgba, 4, texels, f) == texels;
    }
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
        head.texture_count > 4096) {
        fclose(f);
        return NULL;
    }
    Scene *scene = malloc(sizeof(*scene));
    ScenePolygon *polygons = malloc(sizeof(ScenePolygon) * (size_t) (head.polygon_count + 1));
    SceneVertex *vertices = malloc(sizeof(SceneVertex) * (size_t) (head.vertex_count + 1));
    SceneTexture *textures = calloc((size_t) head.texture_count + 1, sizeof(SceneTexture));
    if (scene == NULL || polygons == NULL || vertices == NULL || textures == NULL) {
        free(scene);
        free(polygons);
        free(vertices);
        free(textures);
        fclose(f);
        return NULL;
    }
    *scene = head;
    scene->polygons = polygons;
    scene->vertices = vertices;
    scene->textures = textures;
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
    fclose(f);
    if (!ok) {
        scene_free(scene);
        return NULL;
    }
    return scene;
}
