/* Scenes (scene.h) kept in files, for trying a renderer on them away from the game. */
#ifndef ACWW_PORT_SCENE_FILE_H
#define ACWW_PORT_SCENE_FILE_H

#include <stdio.h>

#include "scene.h"

/* A copy of a scene that owns everything it points to; scene_free gives it back. */
Scene *scene_copy(const Scene *scene);
void scene_free(Scene *scene);

int scene_write(const char *path, const Scene *scene); /* 0, or -1 */
Scene *scene_read(const char *path);                   /* or NULL */

#endif
