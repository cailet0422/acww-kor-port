/* The window drawn through Vulkan (docs/renderer.md): the 3D scene at the window's own size
 * (vk_scene.h), the game's 2D layers mixed with it, and the touch buttons over them.
 *
 * The caller makes the instance and the surface (SDL does, or a headless surface for the
 * checks); everything else is made here. All calls come from one thread.
 */
#ifndef ACWW_PORT_VK_PRESENT_H
#define ACWW_PORT_VK_PRESENT_H

#include "vk_scene.h"

/* A corner of a triangle of the buttons' drawing, in the window's pixels (touch.h). */
typedef struct PresentVertex {
    float x, y;
    uint8_t r, g, b, a;
} PresentVertex;

typedef struct PresentFrame {
    /* the DS's two screens as the game drew them: 256 x 192 each, 0x00RRGGBB */
    const uint32_t *upper, *lower;
    /* the 3D scene to show at the window's size, or NULL: then the screens as they are */
    const Scene *scene;
    int scene_over_upper; /* a tall scene goes on over the upper screen */
    /* the lower screen as the 2D engine drew it with a 3D layer all black, all white and
       absent (mix2d.h: 6-bit red, green, blue from bit 0), which a scene is mixed in by */
    const uint32_t *black, *white, *none;
    uint32_t brightness;       /* MASTER_BRIGHT of the screen the 3D layer is on */
    int top[4], bottom[4];     /* where the screens go in the window: x, y, w, h */
    const PresentVertex *ui;   /* the buttons, as triangles */
    int ui_count;
    int capture;               /* keep the window's picture for vk_present_pixels */
    int scene_percent;         /* the scene's picture against the size its screens have in the
                                  window: 100, or less for a GPU that is not fast enough (0: 100) */
} PresentFrame;

typedef struct VkPresenter VkPresenter;

/* NULL when the device cannot do it; `why` then says what failed. `samples`: of the scene. */
VkPresenter *vk_present_create(const VkApi *api, VkInstance instance, VkSurfaceKHR surface, int width, int height, int samples,
                               const char **why);
void vk_present_destroy(VkPresenter *p);

/* A frame onto the window, whose size is given each time. 0; 1 when the surface is gone
 * (the app left the screen); -1 on another failure. After either the window's owner lets go
 * of the surface (vk_present_surface_gone) and gives a new one (vk_present_surface): what
 * failed is not always told apart by the system. */
int vk_present_frame(VkPresenter *p, const PresentFrame *frame, int width, int height);

/* The surface is let go of, with the swapchain on it: before the window under it goes (an
 * Android app leaving the screen), and before a new surface is made for the same window,
 * which takes one surface at a time. Frames are refused (1) until there is a new one. */
void vk_present_surface_gone(VkPresenter *p);

/* A new surface in place of the old one (which is destroyed here if it still is there).
 * 0; 1 or -1 as vk_present_frame when nothing can be drawn on it. */
int vk_present_surface(VkPresenter *p, VkSurfaceKHR surface, int width, int height);

/* The window's picture of the last frame given with `capture`: rows from the top, four bytes
 * a pixel, blue first when *bgra. NULL when there is none. */
const uint8_t *vk_present_pixels(VkPresenter *p, int *width, int *height, int *bgra);

/* The device's name, for the log. */
const char *vk_present_device(const VkPresenter *p);

#endif
