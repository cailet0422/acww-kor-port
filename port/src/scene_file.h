/* Scenes (scene.h) kept: copies that outlive the engine's frame, and files for trying a
 * renderer on them away from the game. */
#ifndef ACWW_PORT_SCENE_FILE_H
#define ACWW_PORT_SCENE_FILE_H

#include <stdio.h>

#include "scene.h"

/* A copy of a scene that owns everything it points to. It is held once; scene_hold holds it
 * once more (and returns it), scene_free lets go of it once, and it is gone when nobody holds
 * it. Holding and letting go may be done from any thread. */
Scene *scene_copy(const Scene *scene);
Scene *scene_hold(Scene *scene);
void scene_free(Scene *scene);

/* An empty kept scene with room for `polygons` and `vertices`, the registers and the textures
 * of `like`, which it holds for as long as it lives (scene_tween.c fills it). NULL without
 * memory. The arrays are the caller's to write until it hands the scene on. */
Scene *scene_like(Scene *like, int polygons, int vertices, ScenePolygon **polygon_room, SceneVertex **vertex_room);

int scene_write(const char *path, const Scene *scene); /* 0, or -1 */
Scene *scene_read(const char *path);                   /* or NULL */

#endif
