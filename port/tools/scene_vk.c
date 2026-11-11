/* A scene file (port/src/scene_file.h, written by acww-kor --scene-dump) drawn by the Vulkan
 * renderer (port/src/vk_scene.h) into a file of raw pixels, on whatever Vulkan device this
 * machine has: the renderer tried away from the game, and compared with the software's
 * picture of the same frame (port/tests/test_scene_vk.py).
 *
 *     acww-scene-vk SCENE WIDTH HEIGHT OUT [SAMPLES]
 *
 * OUT gets WIDTH x HEIGHT pixels, rows from the top, bytes R G B A: the colour multiplied by
 * the alpha, and the alpha.
 */
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "scene_file.h"
#include "vk_scene.h"

int main(int argc, char **argv)
{
    if (argc < 5) {
        fprintf(stderr, "usage: %s SCENE WIDTH HEIGHT OUT [SAMPLES]\n", argv[0]);
        return 2;
    }
    int width = atoi(argv[2]), height = atoi(argv[3]), samples = argc > 5 ? atoi(argv[5]) : 4;
    Scene *scene = scene_read(argv[1]);
    if (scene == NULL || width <= 0 || height <= 0) {
        fprintf(stderr, "cannot read the scene %s\n", argv[1]);
        return 1;
    }
    /* what the scene uses, for whoever is finding out what the renderer has to do */
    int modes[4] = {0}, translucent = 0, textured = 0, shadows = 0, wire = 0, equal = 0, wbuffer = 0, fogged = 0, formats[8] = {0};
    for (int i = 0; i < scene->polygon_count; i++) {
        const ScenePolygon *p = &scene->polygons[i];
        modes[(p->attr >> 4) & 3]++;
        translucent += p->translucent;
        textured += p->texture >= 0;
        shadows += p->shadow || p->shadow_mask;
        wire += ((p->attr >> 16) & 31) == 0;
        equal += (p->attr >> 14) & 1;
        wbuffer += p->wbuffer;
        fogged += (p->attr >> 15) & 1;
        formats[(p->texparam >> 26) & 7] += p->texture >= 0;
    }
    printf("scene: %d polygons (%d translucent, %d textured, %d shadow, %d wire, %d depth-equal, %d w-buffer, %d fog), "
           "%d vertices, %d textures; modes %d/%d/%d/%d; formats %d %d %d %d %d %d %d; DISP3DCNT %04x, alpha ref %u, %s\n",
           scene->polygon_count, translucent, textured, shadows, wire, equal, wbuffer, fogged, scene->vertex_count,
           scene->texture_count, modes[0], modes[1], modes[2], modes[3], formats[1], formats[2], formats[3], formats[4],
           formats[5], formats[6], formats[7], scene->disp3dcnt, scene->alpha_ref, scene->tall ? "tall" : "one screen");

    void *library = dlopen("libvulkan.so.1", RTLD_NOW);
    if (library == NULL) {
        library = dlopen("libvulkan.so", RTLD_NOW);
    }
    PFN_vkGetInstanceProcAddr get = library != NULL ? (PFN_vkGetInstanceProcAddr) dlsym(library, "vkGetInstanceProcAddr") : NULL;
    VkApi api;
    if (get == NULL || vk_api_load_global(&api, get) != 0) {
        fprintf(stderr, "no Vulkan on this machine\n");
        return 1;
    }
    VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .pApplicationName = "acww-scene-vk", .apiVersion = VK_API_VERSION_1_0};
    VkInstanceCreateInfo instance_info = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app};
    VkInstance instance;
    if (api.CreateInstance(&instance_info, NULL, &instance) != VK_SUCCESS || vk_api_load(&api, get, instance) != 0) {
        fprintf(stderr, "cannot make a Vulkan instance\n");
        return 1;
    }
    VkPhysicalDevice devices[8];
    uint32_t device_count = 8;
    api.EnumeratePhysicalDevices(instance, &device_count, devices);
    const char *chosen = getenv("PORT_VK_DEVICE");
    uint32_t at = chosen != NULL ? (uint32_t) atoi(chosen) : 0;
    if (at >= device_count) {
        fprintf(stderr, "no Vulkan device\n");
        return 1;
    }
    VkPhysicalDeviceProperties props;
    api.GetPhysicalDeviceProperties(devices[at], &props);
    VkQueueFamilyProperties families[16];
    uint32_t family_count = 16, family = 0;
    api.GetPhysicalDeviceQueueFamilyProperties(devices[at], &family_count, families);
    while (family < family_count && !(families[family].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
        family++;
    }
    float priority = 1.0f;
    VkDeviceQueueCreateInfo queue_info = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                                          .queueFamilyIndex = family,
                                          .queueCount = 1,
                                          .pQueuePriorities = &priority};
    VkDeviceCreateInfo device_info = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .queueCreateInfoCount = 1, .pQueueCreateInfos = &queue_info};
    VkDevice device;
    if (family == family_count || api.CreateDevice(devices[at], &device_info, NULL, &device) != VK_SUCCESS) {
        fprintf(stderr, "cannot make a Vulkan device\n");
        return 1;
    }
    VkQueue queue;
    api.GetDeviceQueue(device, family, 0, &queue);
    const char *why = NULL;
    VkSceneRenderer *renderer = vk_scene_create(&api, devices[at], device, family, queue, width, height, samples, &why);
    if (renderer == NULL) {
        fprintf(stderr, "no renderer on %s: %s\n", props.deviceName, why);
        return 1;
    }
    const uint8_t *pixels = vk_scene_draw(renderer, scene) == 0 ? vk_scene_read(renderer) : NULL;
    if (pixels == NULL) {
        fprintf(stderr, "the scene was not drawn\n");
        return 1;
    }
    FILE *f = fopen(argv[4], "wb");
    if (f == NULL || fwrite(pixels, 4, (size_t) width * (size_t) height, f) != (size_t) width * (size_t) height || fclose(f) != 0) {
        fprintf(stderr, "cannot write %s\n", argv[4]);
        return 1;
    }
    printf("drawn on %s: %d x %d, %d samples\n", props.deviceName, width, height, samples);
    vk_scene_destroy(renderer);
    api.DestroyDevice(device, NULL);
    api.DestroyInstance(instance, NULL);
    scene_free(scene);
    return 0;
}
