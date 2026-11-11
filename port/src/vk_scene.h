/* The DS's 3D scene (scene.h) drawn by a GPU through Vulkan, at any size and with
 * multisampling (docs/renderer.md). The software rasteriser of the decomp stays the
 * reference: this draws the same polygons by the same rules, as far as a GPU has them.
 *
 * Vulkan is not linked: its functions are found at run time from vkGetInstanceProcAddr, which
 * whoever opened the library gives (SDL, or dlopen in the tool), so that a build runs where
 * there is no Vulkan at all.
 */
#ifndef ACWW_PORT_VK_SCENE_H
#define ACWW_PORT_VK_SCENE_H

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#include "scene.h"

/* The Vulkan functions used, as pointers (vk_functions.h lists them). */
typedef struct VkApi {
#define VK_FN(name) PFN_vk##name name;
#include "vk_functions.h"
#undef VK_FN
} VkApi;

/* The functions an instance is made with; then the rest, for the instance. 0, or -1 when
 * one is missing. */
int vk_api_load_global(VkApi *api, PFN_vkGetInstanceProcAddr get);
int vk_api_load(VkApi *api, PFN_vkGetInstanceProcAddr get, VkInstance instance);

typedef struct VkSceneRenderer VkSceneRenderer;

/* A renderer drawing into a picture of its own of `width` x `height`, with `samples` samples
 * a pixel (1, 2, 4 or 8; fewer if the device has not that many). NULL when the device cannot
 * do it; `why` then says what failed. */
VkSceneRenderer *vk_scene_create(const VkApi *api, VkPhysicalDevice physical, VkDevice device, uint32_t queue_family,
                                 VkQueue queue, int width, int height, int samples, const char **why);
void vk_scene_destroy(VkSceneRenderer *r);

/* Draws the scene. The picture holds, for each pixel, the 3D layer's colour multiplied by its
 * alpha, and the alpha: 0 where the scene has nothing. 0, or -1. */
int vk_scene_draw(VkSceneRenderer *r, const Scene *scene);

/* The picture just drawn, read back: rows from the top, bytes R G B A. Valid until the next
 * draw. NULL when it cannot be read. */
const uint8_t *vk_scene_read(VkSceneRenderer *r);

/* The picture as an image a later pass can sample (resolved, one sample a pixel). */
VkImageView vk_scene_view(VkSceneRenderer *r);

#endif
