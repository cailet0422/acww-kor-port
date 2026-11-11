/* The window drawn through Vulkan (vk_present.h).
 *
 * A frame is one command buffer: the DS's two screens and the 2D engine's three answers put
 * into textures; the scene drawn into its own picture at the size the screens have in the
 * window (vk_scene.c); then, onto the window, each screen as a rectangle that mixes what
 * belongs to it (port/shaders/compose.frag), and the buttons' triangles over them. One frame
 * is under way at a time: the next waits for it before it touches what the frame uses.
 */
#include "vk_present.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vk_shaders.h"

#define DS_W 256
#define DS_H 192
#define DS_BYTES (DS_W * DS_H * 4)
#define IMAGES 8
#define UI_VERTICES 65536
#define SCENE_MAX 4096 /* the scene's picture, at most, each way */

enum { TEX_UPPER, TEX_LOWER, TEX_BLACK, TEX_WHITE, TEX_NONE, TEX_EMPTY, TEX_COUNT };

typedef struct ScreenConstants {
    float rect[4];
    float scene_uv[4];
    float bright[4];
    float texel_px[2];
    uint32_t kind;
    uint32_t pad;
} ScreenConstants;

typedef struct Image {
    VkImage image;
    VkDeviceMemory memory;
    VkImageView view;
} Image;

typedef struct Mapped {
    VkBuffer buffer;
    VkDeviceMemory memory;
    void *at;
} Mapped;

struct VkPresenter {
    VkApi vk;
    VkInstance instance;
    VkSurfaceKHR surface;
    VkPhysicalDevice physical;
    VkDevice device;
    uint32_t family;
    VkQueue queue;
    char device_name[VK_MAX_PHYSICAL_DEVICE_NAME_SIZE];

    VkSwapchainKHR swapchain;
    VkFormat format;
    VkExtent2D extent;
    uint32_t image_count;
    VkImage images[IMAGES];
    VkImageView views[IMAGES];
    VkFramebuffer framebuffers[IMAGES];
    int can_capture;
    VkRenderPass pass;

    VkDescriptorSetLayout compose_set_layout;
    VkPipelineLayout compose_layout, ui_layout;
    VkPipeline compose, ui;
    VkShaderModule modules[4];
    VkSampler linear;
    VkDescriptorPool pool;
    VkDescriptorSet set_upper, set_lower;
    Image textures[TEX_COUNT];
    Mapped staging, ui_vertices, readback;
    VkDeviceSize readback_size;
    int readback_w, readback_h, readback_valid;

    VkCommandPool commands;
    VkCommandBuffer cmd;
    VkFence fence;
    VkSemaphore acquired, drawn;
    int in_flight;

    VkSceneRenderer *scene;
    int samples;
    int sets_for_scene; /* the descriptor sets name the scene's picture as it is now */
};

/* Small things ------------------------------------------------------------------------------ */

static int memory_type(VkPresenter *p, uint32_t allowed, VkMemoryPropertyFlags wanted)
{
    VkPhysicalDeviceMemoryProperties props;
    p->vk.GetPhysicalDeviceMemoryProperties(p->physical, &props);
    for (uint32_t i = 0; i < props.memoryTypeCount; i++) {
        if ((allowed & (1u << i)) && (props.memoryTypes[i].propertyFlags & wanted) == wanted) {
            return (int) i;
        }
    }
    return -1;
}

static int mapped_make(VkPresenter *p, Mapped *m, VkDeviceSize size, VkBufferUsageFlags usage)
{
    VkBufferCreateInfo info = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = size, .usage = usage};
    memset(m, 0, sizeof(*m));
    if (p->vk.CreateBuffer(p->device, &info, NULL, &m->buffer) != VK_SUCCESS) {
        return -1;
    }
    VkMemoryRequirements need;
    p->vk.GetBufferMemoryRequirements(p->device, m->buffer, &need);
    int type = memory_type(p, need.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VkMemoryAllocateInfo alloc = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                                  .allocationSize = need.size,
                                  .memoryTypeIndex = (uint32_t) type};
    return type >= 0 && p->vk.AllocateMemory(p->device, &alloc, NULL, &m->memory) == VK_SUCCESS &&
                   p->vk.BindBufferMemory(p->device, m->buffer, m->memory, 0) == VK_SUCCESS &&
                   p->vk.MapMemory(p->device, m->memory, 0, VK_WHOLE_SIZE, 0, &m->at) == VK_SUCCESS
               ? 0
               : -1;
}

static void mapped_free(VkPresenter *p, Mapped *m)
{
    if (m->buffer != VK_NULL_HANDLE) {
        p->vk.DestroyBuffer(p->device, m->buffer, NULL);
    }
    if (m->memory != VK_NULL_HANDLE) {
        p->vk.FreeMemory(p->device, m->memory, NULL);
    }
    memset(m, 0, sizeof(*m));
}

static int texture_make(VkPresenter *p, Image *t, int width, int height, VkFormat format)
{
    VkImageCreateInfo info = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                              .imageType = VK_IMAGE_TYPE_2D,
                              .format = format,
                              .extent = {(uint32_t) width, (uint32_t) height, 1},
                              .mipLevels = 1,
                              .arrayLayers = 1,
                              .samples = VK_SAMPLE_COUNT_1_BIT,
                              .tiling = VK_IMAGE_TILING_OPTIMAL,
                              .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                              .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED};
    if (p->vk.CreateImage(p->device, &info, NULL, &t->image) != VK_SUCCESS) {
        return -1;
    }
    VkMemoryRequirements need;
    p->vk.GetImageMemoryRequirements(p->device, t->image, &need);
    int type = memory_type(p, need.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (type < 0) {
        type = memory_type(p, need.memoryTypeBits, 0);
    }
    VkMemoryAllocateInfo alloc = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                                  .allocationSize = need.size,
                                  .memoryTypeIndex = (uint32_t) type};
    VkImageViewCreateInfo view = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                                  .viewType = VK_IMAGE_VIEW_TYPE_2D,
                                  .format = format,
                                  .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
    if (type < 0 || p->vk.AllocateMemory(p->device, &alloc, NULL, &t->memory) != VK_SUCCESS ||
        p->vk.BindImageMemory(p->device, t->image, t->memory, 0) != VK_SUCCESS) {
        return -1;
    }
    view.image = t->image;
    return p->vk.CreateImageView(p->device, &view, NULL, &t->view) == VK_SUCCESS ? 0 : -1;
}

static void barrier(VkPresenter *p, VkImage image, VkImageLayout from, VkImageLayout to, VkAccessFlags src_access,
                    VkAccessFlags dst_access, VkPipelineStageFlags src_stage, VkPipelineStageFlags dst_stage)
{
    VkImageMemoryBarrier b = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                              .srcAccessMask = src_access,
                              .dstAccessMask = dst_access,
                              .oldLayout = from,
                              .newLayout = to,
                              .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                              .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                              .image = image,
                              .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
    p->vk.CmdPipelineBarrier(p->cmd, src_stage, dst_stage, 0, 0, NULL, 0, NULL, 1, &b);
}

/* What is under way is carried out; then everything a frame uses may be written again. */
static void settle(VkPresenter *p)
{
    if (p->in_flight) {
        p->vk.WaitForFences(p->device, 1, &p->fence, VK_TRUE, 5000000000ull);
        p->in_flight = 0;
    }
}

/* The swapchain ----------------------------------------------------------------------------- */

static void swapchain_free(VkPresenter *p)
{
    for (uint32_t i = 0; i < p->image_count; i++) {
        if (p->framebuffers[i] != VK_NULL_HANDLE) {
            p->vk.DestroyFramebuffer(p->device, p->framebuffers[i], NULL);
        }
        if (p->views[i] != VK_NULL_HANDLE) {
            p->vk.DestroyImageView(p->device, p->views[i], NULL);
        }
        p->framebuffers[i] = VK_NULL_HANDLE;
        p->views[i] = VK_NULL_HANDLE;
    }
    p->image_count = 0;
    if (p->swapchain != VK_NULL_HANDLE) {
        p->vk.DestroySwapchainKHR(p->device, p->swapchain, NULL);
        p->swapchain = VK_NULL_HANDLE;
    }
}

/* 0; 1 when the surface is gone; -1. */
static int swapchain_make(VkPresenter *p, int width, int height)
{
    const VkApi *vk = &p->vk;
    settle(p);
    vk->DeviceWaitIdle(p->device);
    VkSurfaceCapabilitiesKHR caps;
    VkResult got = vk->GetPhysicalDeviceSurfaceCapabilitiesKHR(p->physical, p->surface, &caps);
    if (got == VK_ERROR_SURFACE_LOST_KHR) {
        return 1;
    }
    if (got != VK_SUCCESS) {
        return -1;
    }
    VkSurfaceFormatKHR formats[64];
    uint32_t format_count = 64;
    vk->GetPhysicalDeviceSurfaceFormatsKHR(p->physical, p->surface, &format_count, formats);
    if (format_count == 0) {
        return -1;
    }
    /* colours as they are: not a format that converts to sRGB on writing */
    VkSurfaceFormatKHR chosen = formats[0];
    for (uint32_t i = 0; i < format_count; i++) {
        if (formats[i].format == VK_FORMAT_B8G8R8A8_UNORM || formats[i].format == VK_FORMAT_R8G8B8A8_UNORM) {
            chosen = formats[i];
            break;
        }
    }
    /* the newest picture at each refresh where the device can (the emulation keeps its own
       time, a little off the display's); else in turn */
    VkPresentModeKHR modes[16], mode = VK_PRESENT_MODE_FIFO_KHR;
    uint32_t mode_count = 16;
    vk->GetPhysicalDeviceSurfacePresentModesKHR(p->physical, p->surface, &mode_count, modes);
    for (uint32_t i = 0; i < mode_count; i++) {
        if (modes[i] == VK_PRESENT_MODE_MAILBOX_KHR) {
            mode = modes[i];
        }
    }
    VkExtent2D extent = caps.currentExtent;
    if (extent.width == 0xffffffffu) {
        extent.width = (uint32_t) width;
        extent.height = (uint32_t) height;
    }
    extent.width = extent.width < caps.minImageExtent.width ? caps.minImageExtent.width : extent.width;
    extent.height = extent.height < caps.minImageExtent.height ? caps.minImageExtent.height : extent.height;
    if (caps.maxImageExtent.width != 0) {
        extent.width = extent.width > caps.maxImageExtent.width ? caps.maxImageExtent.width : extent.width;
        extent.height = extent.height > caps.maxImageExtent.height ? caps.maxImageExtent.height : extent.height;
    }
    if (extent.width == 0 || extent.height == 0) {
        return 1; /* nothing to draw on (minimised): as good as gone for now */
    }
    uint32_t count = caps.minImageCount + 1;
    if (caps.maxImageCount != 0 && count > caps.maxImageCount) {
        count = caps.maxImageCount;
    }
    static const VkCompositeAlphaFlagBitsKHR alphas[] = {VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
                                                         VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR};
    VkCompositeAlphaFlagBitsKHR alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    for (int i = 3; i >= 0; i--) {
        if (caps.supportedCompositeAlpha & alphas[i]) {
            alpha = alphas[i];
        }
    }
    p->can_capture = (caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0;
    VkSwapchainKHR old = p->swapchain;
    VkSwapchainCreateInfoKHR info = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = p->surface,
        .minImageCount = count,
        .imageFormat = chosen.format,
        .imageColorSpace = chosen.colorSpace,
        .imageExtent = extent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | (p->can_capture ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT : 0),
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        /* drawn upright; a phone turned on its side is the system's to turn */
        .preTransform = (caps.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR) ? VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR
                                                                                              : caps.currentTransform,
        .compositeAlpha = alpha,
        .presentMode = mode,
        .clipped = VK_TRUE,
        .oldSwapchain = old};
    VkSwapchainKHR made = VK_NULL_HANDLE;
    got = vk->CreateSwapchainKHR(p->device, &info, NULL, &made);
    p->swapchain = old;
    swapchain_free(p);
    if (got == VK_ERROR_SURFACE_LOST_KHR) {
        return 1;
    }
    if (got != VK_SUCCESS) {
        return -1;
    }
    p->swapchain = made;
    if (p->pass != VK_NULL_HANDLE && p->format != chosen.format) {
        return -1; /* (the pass and the pipelines were made for the first format) */
    }
    p->format = chosen.format;
    p->extent = extent;
    if (p->pass == VK_NULL_HANDLE) {
        VkAttachmentDescription attachment = {.format = p->format,
                                              .samples = VK_SAMPLE_COUNT_1_BIT,
                                              .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                                              .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                                              .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                                              .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                                              .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                                              .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR};
        VkAttachmentReference ref = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkSubpassDescription subpass = {.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
                                        .colorAttachmentCount = 1,
                                        .pColorAttachments = &ref};
        VkSubpassDependency dependency = {.srcSubpass = VK_SUBPASS_EXTERNAL,
                                          .dstSubpass = 0,
                                          .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                          .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                          .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT};
        VkRenderPassCreateInfo pass = {.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
                                       .attachmentCount = 1,
                                       .pAttachments = &attachment,
                                       .subpassCount = 1,
                                       .pSubpasses = &subpass,
                                       .dependencyCount = 1,
                                       .pDependencies = &dependency};
        if (vk->CreateRenderPass(p->device, &pass, NULL, &p->pass) != VK_SUCCESS) {
            return -1;
        }
    }
    p->image_count = IMAGES;
    if (vk->GetSwapchainImagesKHR(p->device, p->swapchain, &p->image_count, p->images) < 0) {
        p->image_count = 0;
        return -1;
    }
    for (uint32_t i = 0; i < p->image_count; i++) {
        VkImageViewCreateInfo view = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                                      .image = p->images[i],
                                      .viewType = VK_IMAGE_VIEW_TYPE_2D,
                                      .format = p->format,
                                      .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
        VkFramebufferCreateInfo fb = {.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
                                      .renderPass = p->pass,
                                      .attachmentCount = 1,
                                      .width = extent.width,
                                      .height = extent.height,
                                      .layers = 1};
        if (vk->CreateImageView(p->device, &view, NULL, &p->views[i]) != VK_SUCCESS) {
            return -1;
        }
        fb.pAttachments = &p->views[i];
        if (vk->CreateFramebuffer(p->device, &fb, NULL, &p->framebuffers[i]) != VK_SUCCESS) {
            return -1;
        }
    }
    return 0;
}

/* Creation ---------------------------------------------------------------------------------- */

static VkPipeline pipeline_make(VkPresenter *p, VkShaderModule vert, VkShaderModule frag, VkPipelineLayout layout, int ui)
{
    VkPipelineShaderStageCreateInfo stages[2] = {
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vert, .pName = "main"},
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = frag, .pName = "main"},
    };
    VkVertexInputBindingDescription binding = {0, sizeof(PresentVertex), VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attributes[2] = {
        {0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(PresentVertex, x)},
        {1, 0, VK_FORMAT_R8G8B8A8_UNORM, offsetof(PresentVertex, r)},
    };
    VkPipelineVertexInputStateCreateInfo input = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
                                                  .vertexBindingDescriptionCount = ui ? 1u : 0u,
                                                  .pVertexBindingDescriptions = &binding,
                                                  .vertexAttributeDescriptionCount = ui ? 2u : 0u,
                                                  .pVertexAttributeDescriptions = attributes};
    VkPipelineInputAssemblyStateCreateInfo assembly = {.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
                                                       .topology = ui ? VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
                                                                      : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP};
    VkPipelineViewportStateCreateInfo viewport = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
                                                  .viewportCount = 1,
                                                  .scissorCount = 1};
    VkPipelineRasterizationStateCreateInfo raster = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
                                                     .polygonMode = VK_POLYGON_MODE_FILL,
                                                     .cullMode = VK_CULL_MODE_NONE,
                                                     .lineWidth = 1.0f};
    VkPipelineMultisampleStateCreateInfo multisample = {.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
                                                        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT};
    VkPipelineColorBlendAttachmentState attachment = {
        .blendEnable = ui ? VK_TRUE : VK_FALSE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT};
    VkPipelineColorBlendStateCreateInfo blend = {.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
                                                 .attachmentCount = 1,
                                                 .pAttachments = &attachment};
    VkDynamicState dynamics[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic = {.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
                                                .dynamicStateCount = 2,
                                                .pDynamicStates = dynamics};
    VkGraphicsPipelineCreateInfo info = {.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
                                         .stageCount = 2,
                                         .pStages = stages,
                                         .pVertexInputState = &input,
                                         .pInputAssemblyState = &assembly,
                                         .pViewportState = &viewport,
                                         .pRasterizationState = &raster,
                                         .pMultisampleState = &multisample,
                                         .pColorBlendState = &blend,
                                         .pDynamicState = &dynamic,
                                         .layout = layout,
                                         .renderPass = p->pass};
    VkPipeline pipeline = VK_NULL_HANDLE;
    if (p->vk.CreateGraphicsPipelines(p->device, VK_NULL_HANDLE, 1, &info, NULL, &pipeline) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }
    return pipeline;
}

/* The two screens' descriptor sets: the scene's picture (or a picture of nothing), the screen
 * as the game drew it, and the three answers. */
static void sets_write(VkPresenter *p)
{
    VkImageView scene = p->scene != NULL ? vk_scene_view(p->scene) : p->textures[TEX_EMPTY].view;
    for (int lower = 0; lower < 2; lower++) {
        VkDescriptorImageInfo images[5] = {
            {p->linear, scene, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {p->linear, p->textures[lower ? TEX_LOWER : TEX_UPPER].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {p->linear, p->textures[TEX_BLACK].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {p->linear, p->textures[TEX_WHITE].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {p->linear, p->textures[TEX_NONE].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        };
        VkWriteDescriptorSet writes[5];
        for (uint32_t i = 0; i < 5; i++) {
            writes[i] = (VkWriteDescriptorSet){.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                               .dstSet = lower ? p->set_lower : p->set_upper,
                                               .dstBinding = i,
                                               .descriptorCount = 1,
                                               .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                               .pImageInfo = &images[i]};
        }
        p->vk.UpdateDescriptorSets(p->device, 5, writes, 0, NULL);
    }
    p->sets_for_scene = 1;
}

static const char *create(VkPresenter *p, int width, int height)
{
    const VkApi *vk = &p->vk;
    /* a device that draws and shows on this surface */
    VkPhysicalDevice devices[8];
    uint32_t device_count = 8;
    vk->EnumeratePhysicalDevices(p->instance, &device_count, devices);
    p->physical = VK_NULL_HANDLE;
    for (uint32_t d = 0; d < device_count && p->physical == VK_NULL_HANDLE; d++) {
        VkQueueFamilyProperties families[16];
        uint32_t family_count = 16;
        vk->GetPhysicalDeviceQueueFamilyProperties(devices[d], &family_count, families);
        for (uint32_t f = 0; f < family_count; f++) {
            VkBool32 shows = VK_FALSE;
            vk->GetPhysicalDeviceSurfaceSupportKHR(devices[d], f, p->surface, &shows);
            if ((families[f].queueFlags & VK_QUEUE_GRAPHICS_BIT) && shows) {
                p->physical = devices[d];
                p->family = f;
                break;
            }
        }
    }
    if (p->physical == VK_NULL_HANDLE) {
        return "no Vulkan device shows on this window";
    }
    VkPhysicalDeviceProperties props;
    vk->GetPhysicalDeviceProperties(p->physical, &props);
    snprintf(p->device_name, sizeof(p->device_name), "%s", props.deviceName);
    float priority = 1.0f;
    const char *extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkDeviceQueueCreateInfo queue = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                                     .queueFamilyIndex = p->family,
                                     .queueCount = 1,
                                     .pQueuePriorities = &priority};
    VkDeviceCreateInfo device = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
                                 .queueCreateInfoCount = 1,
                                 .pQueueCreateInfos = &queue,
                                 .enabledExtensionCount = 1,
                                 .ppEnabledExtensionNames = extensions};
    if (vk->CreateDevice(p->physical, &device, NULL, &p->device) != VK_SUCCESS) {
        return "the device has no swapchain";
    }
    vk->GetDeviceQueue(p->device, p->family, 0, &p->queue);

    VkCommandPoolCreateInfo cpool = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                                     .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
                                     .queueFamilyIndex = p->family};
    VkCommandBufferAllocateInfo cbuf = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                                        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                                        .commandBufferCount = 1};
    VkFenceCreateInfo fence = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VkSemaphoreCreateInfo semaphore = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    if (vk->CreateCommandPool(p->device, &cpool, NULL, &p->commands) != VK_SUCCESS) {
        return "no command pool";
    }
    cbuf.commandPool = p->commands;
    if (vk->AllocateCommandBuffers(p->device, &cbuf, &p->cmd) != VK_SUCCESS ||
        vk->CreateFence(p->device, &fence, NULL, &p->fence) != VK_SUCCESS ||
        vk->CreateSemaphore(p->device, &semaphore, NULL, &p->acquired) != VK_SUCCESS ||
        vk->CreateSemaphore(p->device, &semaphore, NULL, &p->drawn) != VK_SUCCESS) {
        return "no command buffer";
    }
    if (swapchain_make(p, width, height) != 0) {
        return "no swapchain";
    }

    VkDescriptorSetLayoutBinding bindings[5];
    for (uint32_t i = 0; i < 5; i++) {
        bindings[i] = (VkDescriptorSetLayoutBinding){i, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, NULL};
    }
    VkDescriptorSetLayoutCreateInfo set_layout = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                                                  .bindingCount = 5,
                                                  .pBindings = bindings};
    if (vk->CreateDescriptorSetLayout(p->device, &set_layout, NULL, &p->compose_set_layout) != VK_SUCCESS) {
        return "no descriptor layout";
    }
    VkPushConstantRange screen_range = {VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(ScreenConstants)};
    VkPipelineLayoutCreateInfo layout = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                                         .setLayoutCount = 1,
                                         .pSetLayouts = &p->compose_set_layout,
                                         .pushConstantRangeCount = 1,
                                         .pPushConstantRanges = &screen_range};
    VkPushConstantRange window_range = {VK_SHADER_STAGE_VERTEX_BIT, 0, 2 * sizeof(float)};
    VkPipelineLayoutCreateInfo ui_layout = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                                            .pushConstantRangeCount = 1,
                                            .pPushConstantRanges = &window_range};
    if (vk->CreatePipelineLayout(p->device, &layout, NULL, &p->compose_layout) != VK_SUCCESS ||
        vk->CreatePipelineLayout(p->device, &ui_layout, NULL, &p->ui_layout) != VK_SUCCESS) {
        return "no pipeline layout";
    }
    const uint32_t *codes[4] = {shader_compose_vert, shader_compose_frag, shader_ui_vert, shader_ui_frag};
    const size_t sizes[4] = {sizeof(shader_compose_vert), sizeof(shader_compose_frag), sizeof(shader_ui_vert), sizeof(shader_ui_frag)};
    for (int i = 0; i < 4; i++) {
        VkShaderModuleCreateInfo module = {.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, .codeSize = sizes[i], .pCode = codes[i]};
        if (vk->CreateShaderModule(p->device, &module, NULL, &p->modules[i]) != VK_SUCCESS) {
            return "a shader was refused";
        }
    }
    p->compose = pipeline_make(p, p->modules[0], p->modules[1], p->compose_layout, 0);
    p->ui = pipeline_make(p, p->modules[2], p->modules[3], p->ui_layout, 1);
    if (p->compose == VK_NULL_HANDLE || p->ui == VK_NULL_HANDLE) {
        return "no pipeline";
    }
    VkSamplerCreateInfo sampler = {.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
                                   .magFilter = VK_FILTER_LINEAR,
                                   .minFilter = VK_FILTER_LINEAR,
                                   .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
                                   .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                                   .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                                   .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                                   .maxAnisotropy = 1.0f};
    if (vk->CreateSampler(p->device, &sampler, NULL, &p->linear) != VK_SUCCESS) {
        return "no sampler";
    }
    for (int i = 0; i < TEX_COUNT; i++) {
        /* the screens come as 0x00RRGGBB, blue first in memory; the answers are made red first */
        VkFormat format = i == TEX_UPPER || i == TEX_LOWER ? VK_FORMAT_B8G8R8A8_UNORM : VK_FORMAT_R8G8B8A8_UNORM;
        int w = i == TEX_EMPTY ? 1 : DS_W, h = i == TEX_EMPTY ? 1 : DS_H;
        if (texture_make(p, &p->textures[i], w, h, format) != 0) {
            return "no memory for the screens";
        }
    }
    if (mapped_make(p, &p->staging, (VkDeviceSize) DS_BYTES * 5 + 4, VK_BUFFER_USAGE_TRANSFER_SRC_BIT) != 0 ||
        mapped_make(p, &p->ui_vertices, UI_VERTICES * sizeof(PresentVertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT) != 0) {
        return "no memory for the buffers";
    }
    VkDescriptorPoolSize pool_size = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 10};
    VkDescriptorPoolCreateInfo pool = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
                                       .maxSets = 2,
                                       .poolSizeCount = 1,
                                       .pPoolSizes = &pool_size};
    VkDescriptorSetLayout layouts[2] = {p->compose_set_layout, p->compose_set_layout};
    VkDescriptorSet sets[2];
    VkDescriptorSetAllocateInfo alloc = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                                         .descriptorSetCount = 2,
                                         .pSetLayouts = layouts};
    if (vk->CreateDescriptorPool(p->device, &pool, NULL, &p->pool) != VK_SUCCESS) {
        return "no descriptor pool";
    }
    alloc.descriptorPool = p->pool;
    if (vk->AllocateDescriptorSets(p->device, &alloc, sets) != VK_SUCCESS) {
        return "no descriptor sets";
    }
    p->set_upper = sets[0];
    p->set_lower = sets[1];
    return NULL;
}

VkPresenter *vk_present_create(const VkApi *api, VkInstance instance, VkSurfaceKHR surface, int width, int height, int samples,
                               const char **why)
{
    VkPresenter *p = calloc(1, sizeof(*p));
    const char *sink;
    why = why != NULL ? why : &sink;
    if (p == NULL) {
        *why = "no memory";
        return NULL;
    }
    p->vk = *api;
    p->instance = instance;
    p->surface = surface;
    p->samples = samples;
    *why = create(p, width, height);
    if (*why != NULL) {
        p->surface = VK_NULL_HANDLE; /* the caller's still */
        vk_present_destroy(p);
        return NULL;
    }
    return p;
}

void vk_present_destroy(VkPresenter *p)
{
    if (p == NULL) {
        return;
    }
    const VkApi *vk = &p->vk;
    if (p->device != VK_NULL_HANDLE) {
        settle(p);
        vk->DeviceWaitIdle(p->device);
        vk_scene_destroy(p->scene);
        swapchain_free(p);
        mapped_free(p, &p->staging);
        mapped_free(p, &p->ui_vertices);
        mapped_free(p, &p->readback);
        for (int i = 0; i < TEX_COUNT; i++) {
            if (p->textures[i].view != VK_NULL_HANDLE) {
                vk->DestroyImageView(p->device, p->textures[i].view, NULL);
            }
            if (p->textures[i].image != VK_NULL_HANDLE) {
                vk->DestroyImage(p->device, p->textures[i].image, NULL);
            }
            if (p->textures[i].memory != VK_NULL_HANDLE) {
                vk->FreeMemory(p->device, p->textures[i].memory, NULL);
            }
        }
        if (p->pool != VK_NULL_HANDLE) {
            vk->DestroyDescriptorPool(p->device, p->pool, NULL);
        }
        if (p->linear != VK_NULL_HANDLE) {
            vk->DestroySampler(p->device, p->linear, NULL);
        }
        if (p->compose != VK_NULL_HANDLE) {
            vk->DestroyPipeline(p->device, p->compose, NULL);
        }
        if (p->ui != VK_NULL_HANDLE) {
            vk->DestroyPipeline(p->device, p->ui, NULL);
        }
        for (int i = 0; i < 4; i++) {
            if (p->modules[i] != VK_NULL_HANDLE) {
                vk->DestroyShaderModule(p->device, p->modules[i], NULL);
            }
        }
        if (p->compose_layout != VK_NULL_HANDLE) {
            vk->DestroyPipelineLayout(p->device, p->compose_layout, NULL);
        }
        if (p->ui_layout != VK_NULL_HANDLE) {
            vk->DestroyPipelineLayout(p->device, p->ui_layout, NULL);
        }
        if (p->compose_set_layout != VK_NULL_HANDLE) {
            vk->DestroyDescriptorSetLayout(p->device, p->compose_set_layout, NULL);
        }
        if (p->pass != VK_NULL_HANDLE) {
            vk->DestroyRenderPass(p->device, p->pass, NULL);
        }
        if (p->fence != VK_NULL_HANDLE) {
            vk->DestroyFence(p->device, p->fence, NULL);
        }
        if (p->acquired != VK_NULL_HANDLE) {
            vk->DestroySemaphore(p->device, p->acquired, NULL);
        }
        if (p->drawn != VK_NULL_HANDLE) {
            vk->DestroySemaphore(p->device, p->drawn, NULL);
        }
        if (p->commands != VK_NULL_HANDLE) {
            vk->DestroyCommandPool(p->device, p->commands, NULL);
        }
        vk->DestroyDevice(p->device, NULL);
    }
    if (p->surface != VK_NULL_HANDLE) {
        vk->DestroySurfaceKHR(p->instance, p->surface, NULL);
    }
    free(p);
}

int vk_present_surface(VkPresenter *p, VkSurfaceKHR surface, int width, int height)
{
    settle(p);
    p->vk.DeviceWaitIdle(p->device);
    swapchain_free(p);
    if (p->surface != VK_NULL_HANDLE) {
        p->vk.DestroySurfaceKHR(p->instance, p->surface, NULL);
    }
    p->surface = surface;
    return swapchain_make(p, width, height);
}

const char *vk_present_device(const VkPresenter *p)
{
    return p->device_name;
}

/* A frame ----------------------------------------------------------------------------------- */

/* A screen or an answer from the staging buffer into its texture. */
static void texture_fill(VkPresenter *p, int which, VkDeviceSize offset, int width, int height)
{
    VkImage image = p->textures[which].image;
    barrier(p, image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
    VkBufferImageCopy region = {.bufferOffset = offset,
                                .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                                .imageExtent = {(uint32_t) width, (uint32_t) height, 1}};
    p->vk.CmdCopyBufferToImage(p->cmd, p->staging.buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    barrier(p, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
}

/* One of the 2D engine's answers (6-bit red, green and blue from bit 0) as bytes. */
static void answer_bytes(uint8_t *to, const uint32_t *from)
{
    for (int i = 0; i < DS_W * DS_H; i++) {
        uint32_t v = from[i];
        to[4 * i] = (uint8_t) ((v & 63) * 255 / 63);
        to[4 * i + 1] = (uint8_t) (((v >> 6) & 63) * 255 / 63);
        to[4 * i + 2] = (uint8_t) (((v >> 12) & 63) * 255 / 63);
        to[4 * i + 3] = 255;
    }
}

/* The scene's renderer at the size the screens have in the window (made anew when that
 * changed); NULL when there is none to be had. */
static VkSceneRenderer *scene_renderer(VkPresenter *p, int width, int height)
{
    width = width < 1 ? 1 : width > SCENE_MAX ? SCENE_MAX : width;
    height = height < 1 ? 1 : height > SCENE_MAX ? SCENE_MAX : height;
    if (p->scene != NULL) {
        int w, h;
        vk_scene_size(p->scene, &w, &h);
        if (w == width && h == height) {
            return p->scene;
        }
        p->vk.DeviceWaitIdle(p->device);
        vk_scene_destroy(p->scene);
        p->scene = NULL;
        p->sets_for_scene = 0;
    }
    const char *why = NULL;
    p->scene = vk_scene_create(&p->vk, p->physical, p->device, p->family, p->queue, width, height, p->samples, &why);
    p->sets_for_scene = 0;
    if (p->scene == NULL) {
        fprintf(stderr, "vulkan: no renderer for the scene at %d x %d (%s)\n", width, height, why);
    }
    return p->scene;
}

static void screen_draw(VkPresenter *p, const int rect[4], uint32_t kind, const float scene_uv[4], uint32_t brightness, int lower)
{
    ScreenConstants c;
    memset(&c, 0, sizeof(c));
    float w = (float) p->extent.width, h = (float) p->extent.height;
    c.rect[0] = (float) rect[0] / w * 2.0f - 1.0f;
    c.rect[1] = (float) rect[1] / h * 2.0f - 1.0f;
    c.rect[2] = (float) (rect[0] + rect[2]) / w * 2.0f - 1.0f;
    c.rect[3] = (float) (rect[1] + rect[3]) / h * 2.0f - 1.0f;
    memcpy(c.scene_uv, scene_uv, sizeof(c.scene_uv));
    /* MASTER_BRIGHT: mode 1 towards white, 2 towards black, by factor / 16 */
    uint32_t mode = (brightness >> 14) & 3, factor = brightness & 31;
    float f = (float) (factor > 16 ? 16 : factor) / 16.0f;
    c.bright[0] = mode == 1 ? f : 0.0f;
    c.bright[1] = mode == 2 ? f : 0.0f;
    c.texel_px[0] = (float) rect[2] / DS_W;
    c.texel_px[1] = (float) rect[3] / DS_H;
    c.kind = kind;
    p->vk.CmdBindDescriptorSets(p->cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p->compose_layout, 0, 1, lower ? &p->set_lower : &p->set_upper,
                                0, NULL);
    p->vk.CmdPushConstants(p->cmd, p->compose_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(c), &c);
    p->vk.CmdDraw(p->cmd, 4, 1, 0, 0);
}

int vk_present_frame(VkPresenter *p, const PresentFrame *frame, int width, int height)
{
    const VkApi *vk = &p->vk;
    settle(p);
    if (p->swapchain == VK_NULL_HANDLE || (int) p->extent.width != width || (int) p->extent.height != height) {
        int made = swapchain_make(p, width, height);
        if (made != 0) {
            return made;
        }
    }
    uint32_t at = 0;
    VkResult got = vk->AcquireNextImageKHR(p->device, p->swapchain, 2000000000ull, p->acquired, VK_NULL_HANDLE, &at);
    if (got == VK_ERROR_OUT_OF_DATE_KHR) {
        int made = swapchain_make(p, width, height);
        if (made != 0) {
            return made;
        }
        got = vk->AcquireNextImageKHR(p->device, p->swapchain, 2000000000ull, p->acquired, VK_NULL_HANDLE, &at);
    }
    if (got == VK_ERROR_SURFACE_LOST_KHR) {
        return 1;
    }
    if (got != VK_SUCCESS && got != VK_SUBOPTIMAL_KHR) {
        return -1;
    }

    /* the scene's picture: as large as its screens are in the window */
    int tall = frame->scene != NULL && frame->scene->tall;
    VkSceneRenderer *scene = NULL;
    if (frame->scene != NULL && frame->black != NULL && frame->white != NULL && frame->none != NULL) {
        scene = scene_renderer(p, frame->bottom[2], tall ? frame->top[3] + frame->bottom[3] : frame->bottom[3]);
    }
    if (!p->sets_for_scene) {
        sets_write(p);
    }

    VkCommandBufferBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                                      .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
    if (vk->ResetCommandBuffer(p->cmd, 0) != VK_SUCCESS || vk->BeginCommandBuffer(p->cmd, &begin) != VK_SUCCESS) {
        return -1;
    }
    uint8_t *staging = p->staging.at;
    memcpy(staging, frame->upper, DS_BYTES);
    memcpy(staging + DS_BYTES, frame->lower, DS_BYTES);
    texture_fill(p, TEX_UPPER, 0, DS_W, DS_H);
    texture_fill(p, TEX_LOWER, DS_BYTES, DS_W, DS_H);
    if (scene != NULL) {
        answer_bytes(staging + 2 * DS_BYTES, frame->black);
        answer_bytes(staging + 3 * DS_BYTES, frame->white);
        answer_bytes(staging + 4 * DS_BYTES, frame->none);
    } else {
        memset(staging + 2 * DS_BYTES, 0, 3 * DS_BYTES);
    }
    texture_fill(p, TEX_BLACK, 2 * DS_BYTES, DS_W, DS_H);
    texture_fill(p, TEX_WHITE, 3 * DS_BYTES, DS_W, DS_H);
    texture_fill(p, TEX_NONE, 4 * DS_BYTES, DS_W, DS_H);
    memset(staging + 5 * DS_BYTES, 0, 4);
    texture_fill(p, TEX_EMPTY, 5 * DS_BYTES, 1, 1);
    if (scene != NULL && vk_scene_record(scene, p->cmd, frame->scene) != 0) {
        return -1;
    }
    int ui_count = frame->ui_count > UI_VERTICES ? UI_VERTICES : frame->ui_count;
    if (ui_count > 0) {
        memcpy(p->ui_vertices.at, frame->ui, (size_t) ui_count * sizeof(PresentVertex));
    }

    VkClearValue clear;
    memset(&clear, 0, sizeof(clear));
    clear.color.float32[3] = 1.0f;
    VkRenderPassBeginInfo pass = {.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
                                  .renderPass = p->pass,
                                  .framebuffer = p->framebuffers[at],
                                  .renderArea = {{0, 0}, p->extent},
                                  .clearValueCount = 1,
                                  .pClearValues = &clear};
    vk->CmdBeginRenderPass(p->cmd, &pass, VK_SUBPASS_CONTENTS_INLINE);
    VkViewport viewport = {0.0f, 0.0f, (float) p->extent.width, (float) p->extent.height, 0.0f, 1.0f};
    VkRect2D scissor = {{0, 0}, p->extent};
    vk->CmdSetViewport(p->cmd, 0, 1, &viewport);
    vk->CmdSetScissor(p->cmd, 0, 1, &scissor);
    vk->CmdBindPipeline(p->cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p->compose);
    static const float whole[4] = {0.0f, 0.0f, 1.0f, 1.0f}, upper_half[4] = {0.0f, 0.0f, 1.0f, 0.5f},
                       lower_half[4] = {0.0f, 0.5f, 1.0f, 1.0f};
    screen_draw(p, frame->top, scene != NULL && tall && frame->scene_over_upper ? 1 : 0, upper_half, frame->brightness, 0);
    screen_draw(p, frame->bottom, scene != NULL ? 2 : 0, tall ? lower_half : whole, frame->brightness, 1);
    if (ui_count > 0) {
        float size[2] = {(float) p->extent.width, (float) p->extent.height};
        VkDeviceSize zero = 0;
        vk->CmdBindPipeline(p->cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p->ui);
        vk->CmdBindVertexBuffers(p->cmd, 0, 1, &p->ui_vertices.buffer, &zero);
        vk->CmdPushConstants(p->cmd, p->ui_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(size), size);
        vk->CmdDraw(p->cmd, (uint32_t) ui_count, 1, 0, 0);
    }
    vk->CmdEndRenderPass(p->cmd);

    p->readback_valid = 0;
    int capture = frame->capture && p->can_capture;
    if (capture) {
        VkDeviceSize bytes = (VkDeviceSize) p->extent.width * p->extent.height * 4;
        if (p->readback.buffer == VK_NULL_HANDLE || p->readback_size != bytes) {
            mapped_free(p, &p->readback);
            capture = mapped_make(p, &p->readback, bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT) == 0;
            p->readback_size = bytes;
        }
    }
    if (capture) {
        barrier(p, p->images[at], VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
        VkBufferImageCopy region = {.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                                    .imageExtent = {p->extent.width, p->extent.height, 1}};
        vk->CmdCopyImageToBuffer(p->cmd, p->images[at], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, p->readback.buffer, 1, &region);
        barrier(p, p->images[at], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_ACCESS_TRANSFER_READ_BIT, 0,
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
    }
    if (vk->EndCommandBuffer(p->cmd) != VK_SUCCESS) {
        return -1;
    }
    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                           .waitSemaphoreCount = 1,
                           .pWaitSemaphores = &p->acquired,
                           .pWaitDstStageMask = &wait_stage,
                           .commandBufferCount = 1,
                           .pCommandBuffers = &p->cmd,
                           .signalSemaphoreCount = 1,
                           .pSignalSemaphores = &p->drawn};
    vk->ResetFences(p->device, 1, &p->fence);
    if (vk->QueueSubmit(p->queue, 1, &submit, p->fence) != VK_SUCCESS) {
        return -1;
    }
    p->in_flight = 1;
    if (capture) {
        settle(p);
        p->readback_w = (int) p->extent.width;
        p->readback_h = (int) p->extent.height;
        p->readback_valid = 1;
    }
    VkPresentInfoKHR present = {.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
                                .waitSemaphoreCount = 1,
                                .pWaitSemaphores = &p->drawn,
                                .swapchainCount = 1,
                                .pSwapchains = &p->swapchain,
                                .pImageIndices = &at};
    got = vk->QueuePresentKHR(p->queue, &present);
    if (got == VK_ERROR_SURFACE_LOST_KHR) {
        return 1;
    }
    if (got == VK_ERROR_OUT_OF_DATE_KHR || got == VK_SUBOPTIMAL_KHR) {
        p->extent.width = 0; /* made anew at the next frame */
    }
    return 0;
}

const uint8_t *vk_present_pixels(VkPresenter *p, int *width, int *height, int *bgra)
{
    if (!p->readback_valid) {
        return NULL;
    }
    *width = p->readback_w;
    *height = p->readback_h;
    *bgra = p->format == VK_FORMAT_B8G8R8A8_UNORM;
    return p->readback.at;
}
