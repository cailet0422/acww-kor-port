/* A scene between two frames' scenes (docs/renderer.md, "Sixty frames").
 *
 * The game draws its 3D scene twenty times a second and the screen shows sixty pictures:
 * each scene three times. Between two scenes that follow one another,
 * scenes of the moments between are made here: every polygon of the later scene that was
 * also given in the earlier one has its corners part of the way from the one to the other.
 * Nothing of the game runs faster for it.
 *
 * A polygon is found in the earlier scene by what does not move (its attributes, its texture
 * and texture coordinates, or those moved as a whole: water, whose texture flows) and by its
 * place in the order the game gives the polygons in, which is the same from scene to scene
 * for a thing; where the orders part, among those near by where they agree again. One that
 * is not found (a cut to another view, a thing that was not there) is left where the later
 * scene has it.
 * The polygons are the game's own, before clipping: the renderer clips them where they now
 * lie.
 */
#ifndef ACWW_PORT_SCENE_TWEEN_H
#define ACWW_PORT_SCENE_TWEEN_H

#include "scene_file.h"

/* The scene the part `t` of the way (0 to 1) from `earlier` to `later` (both kept scenes:
 * scene_file.h), kept, with the later one's order, registers and textures; NULL without
 * memory. `moved` and `of`, if given, take how many of the later scene's polygons were put on
 * the way, and how many it has. */
Scene *scene_between(const Scene *earlier, Scene *later, float t, int *moved, int *of);

/* Of the polygons of all the scenes made so far: [0] found as they were, [1] found with their
 * texture flowed, [2] not noted as given (more in a frame than there is room for), [3] not
 * found, none alike in the earlier scene, [4] not found, those alike too far away. */
void scene_between_counts(unsigned long counts[5]);

/* A check, for two scenes that give the same polygons in the same order (then each polygon's
 * own place is the right one): how many of the polygons found were found in another's place;
 * `checked` takes how many were found, 0 when the scenes are not such a pair. */
int scene_between_same(const Scene *earlier, const Scene *later, int *checked);

#endif
