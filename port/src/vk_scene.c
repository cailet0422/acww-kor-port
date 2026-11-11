/* The DS's 3D scene drawn through Vulkan (vk_scene.h, docs/renderer.md).
 *
 * What the DS does to a pixel, and what is done here for it:
 *
 *   - the polygons come transformed, lit, clipped and in drawing order (scene.h); each is
 *     drawn as a fan of triangles from its clip coordinates, so the size of the picture is
 *     the renderer's to choose;
 *   - textures are decoded by the engine's own reader and used with nearest sampling and the
 *     DS's repeating, flipping or clamping; colours, texture coordinates and depth are
 *     interpolated with perspective, as on the DS;
 *   - modulation, decal, toon and highlight shading, the alpha test and fog are the
 *     fragment shader (port/shaders/scene.frag);
 *   - an opaque pixel is written with its depth. A translucent pixel is blended, writes its
 *     depth only if the polygon says so, and is not drawn over a translucent pixel of the
 *     same polygon ID: the stencil holds the ID a translucent pixel was last written with,
 *     and an opaque pixel clears it. A translucent polygon's opaque pixels (a texture with
 *     its own alpha) are drawn as opaque ones, in a first pass over the polygon;
 *   - the picture holds colour multiplied by alpha, and alpha: blending "over" then gives,
 *     where the scene is translucent over nothing, the colour and the alpha the 2D layers
 *     are mixed in by, and multisampling's averages are right at the scene's outline.
 *
 * Not done yet: shadow volumes, edge marking, wire frames, the rear plane's bitmap. The DS's
 * own anti-aliasing is what multisampling is in place of.
 */
#include "vk_scene.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vk_shaders.h"

#define TEXTURES 512
#define SAMPLERS 9            /* how s repeats (clamp, repeat, mirror) by how t does */
#define PIPELINES 8           /* translucent, depth written, depth "equal" */
#define MAX_VERTICES (2048 * 8 * 3)
#define STENCIL_TRANSLUCENT 0x40u

/* scene.frag's flags */
#define F_TEXTURED 1u
#define F_HIGHLIGHT 8u
#define F_FOG 16u
#define F_FOG_ALPHA_ONLY 32u
#define F_ONLY_OPAQUE 64u
#define F_ONLY_TRANSLUCENT 128u
#define F_W_BUFFER 256u

typedef struct FrameUniforms {
    float toon[32][4];
    float fog_color[4];
    float fog_table[36];
    float fog_offset, fog_scale, alpha_ref, pad;
} FrameUniforms;

typedef struct PolygonConstants {
    float inv_size[2];
    float alpha;
    uint32_t flags;
} PolygonConstants;

typedef struct Texture {
    uint32_t texparam, pltt, hash, used;
    int width, height;
    VkImage image;
    VkDeviceMemory memory;
    VkImageView view;
    VkDescriptorSet set[SAMPLERS];
    uint8_t set_ready[SAMPLERS];
} Texture;

typedef struct Buffer {
    VkBuffer buffer;
    VkDeviceMemory memory;
    VkDeviceSize size;
    void *mapped;
} Buffer;

struct VkSceneRenderer {
    VkApi vk;
    VkPhysicalDevice physical;
    VkDevice device;
    VkQueue queue;
    uint32_t family;
    int width, height;
    VkSampleCountFlagBits samples;
    VkFormat depth_format;
    VkImage color, depth, output;
    VkDeviceMemory color_memory, depth_memory, output_memory;
    VkImageView color_view, depth_view, output_view;
    VkRenderPass pass;
    VkFramebuffer framebuffer;
    VkDescriptorSetLayout frame_layout, texture_layout;
    VkPipelineLayout layout;
    VkShaderModule vert, frag;
    VkPipeline pipeline[PIPELINES];
    VkSampler sampler[SAMPLERS];
    VkDescriptorPool pool;
    VkDescriptorSet frame_set;
    Buffer vertices, uniforms, staging, readback;
    VkCommandPool commands;
    VkCommandBuffer cmd;
    VkFence fence;
    Texture textures[TEXTURES];
    Texture white; /* for polygons without a texture: something has to be bound */
    uint32_t frame;
    int read_valid;
};

/* The functions ----------------------------------------------------------------------------- */

int vk_api_load_global(VkApi *api, PFN_vkGetInstanceProcAddr get)
{
    memset(api, 0, sizeof(*api));
    api->CreateInstance = (PFN_vkCreateInstance) get(NULL, "vkCreateInstance");
    api->EnumerateInstanceExtensionProperties =
        (PFN_vkEnumerateInstanceExtensionProperties) get(NULL, "vkEnumerateInstanceExtensionProperties");
    api->EnumerateInstanceLayerProperties = (PFN_vkEnumerateInstanceLayerProperties) get(NULL, "vkEnumerateInstanceLayerProperties");
    return api->CreateInstance != NULL && api->EnumerateInstanceExtensionProperties != NULL ? 0 : -1;
}

int vk_api_load(VkApi *api, PFN_vkGetInstanceProcAddr get, VkInstance instance)
{
    int missing = 0;
#define VK_FN(name)                                                  \
    if (api->name == NULL) {                                         \
        api->name = (PFN_vk##name) get(instance, "vk" #name);        \
        missing |= api->name == NULL;                                \
    }
#include "vk_functions.h"
#undef VK_FN
    return missing ? -1 : 0;
}

/* Memory, buffers, images ------------------------------------------------------------------- */

static int memory_type(VkSceneRenderer *r, uint32_t allowed, VkMemoryPropertyFlags wanted)
{
    VkPhysicalDeviceMemoryProperties props;
    r->vk.GetPhysicalDeviceMemoryProperties(r->physical, &props);
    for (uint32_t i = 0; i < props.memoryTypeCount; i++) {
        if ((allowed & (1u << i)) && (props.memoryTypes[i].propertyFlags & wanted) == wanted) {
            return (int) i;
        }
    }
    return -1;
}

static int buffer_make(VkSceneRenderer *r, Buffer *b, VkDeviceSize size, VkBufferUsageFlags usage)
{
    VkBufferCreateInfo info = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = size, .usage = usage};
    memset(b, 0, sizeof(*b));
    if (r->vk.CreateBuffer(r->device, &info, NULL, &b->buffer) != VK_SUCCESS) {
        return -1;
    }
    VkMemoryRequirements need;
    r->vk.GetBufferMemoryRequirements(r->device, b->buffer, &need);
    int type = memory_type(r, need.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VkMemoryAllocateInfo alloc = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                                  .allocationSize = need.size,
                                  .memoryTypeIndex = (uint32_t) type};
    if (type < 0 || r->vk.AllocateMemory(r->device, &alloc, NULL, &b->memory) != VK_SUCCESS ||
        r->vk.BindBufferMemory(r->device, b->buffer, b->memory, 0) != VK_SUCCESS ||
        r->vk.MapMemory(r->device, b->memory, 0, VK_WHOLE_SIZE, 0, &b->mapped) != VK_SUCCESS) {
        return -1;
    }
    b->size = size;
    return 0;
}

static void buffer_free(VkSceneRenderer *r, Buffer *b)
{
    if (b->buffer != VK_NULL_HANDLE) {
        r->vk.DestroyBuffer(r->device, b->buffer, NULL);
    }
    if (b->memory != VK_NULL_HANDLE) {
        r->vk.FreeMemory(r->device, b->memory, NULL);
    }
    memset(b, 0, sizeof(*b));
}

static int image_make(VkSceneRenderer *r, int width, int height, VkFormat format, VkSampleCountFlagBits samples,
                      VkImageUsageFlags usage, VkImageAspectFlags aspect, VkImage *image, VkDeviceMemory *memory, VkImageView *view)
{
    VkImageCreateInfo info = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                              .imageType = VK_IMAGE_TYPE_2D,
                              .format = format,
                              .extent = {(uint32_t) width, (uint32_t) height, 1},
                              .mipLevels = 1,
                              .arrayLayers = 1,
                              .samples = samples,
                              .tiling = VK_IMAGE_TILING_OPTIMAL,
                              .usage = usage,
                              .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED};
    if (r->vk.CreateImage(r->device, &info, NULL, image) != VK_SUCCESS) {
        return -1;
    }
    VkMemoryRequirements need;
    r->vk.GetImageMemoryRequirements(r->device, *image, &need);
    int type = memory_type(r, need.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (type < 0) {
        type = memory_type(r, need.memoryTypeBits, 0);
    }
    VkMemoryAllocateInfo alloc = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                                  .allocationSize = need.size,
                                  .memoryTypeIndex = (uint32_t) type};
    if (type < 0 || r->vk.AllocateMemory(r->device, &alloc, NULL, memory) != VK_SUCCESS ||
        r->vk.BindImageMemory(r->device, *image, *memory, 0) != VK_SUCCESS) {
        return -1;
    }
    VkImageViewCreateInfo vinfo = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                                   .image = *image,
                                   .viewType = VK_IMAGE_VIEW_TYPE_2D,
                                   .format = format,
                                   .subresourceRange = {aspect, 0, 1, 0, 1}};
    return r->vk.CreateImageView(r->device, &vinfo, NULL, view) == VK_SUCCESS ? 0 : -1;
}

static void image_free(VkSceneRenderer *r, VkImage *image, VkDeviceMemory *memory, VkImageView *view)
{
    if (*view != VK_NULL_HANDLE) {
        r->vk.DestroyImageView(r->device, *view, NULL);
    }
    if (*image != VK_NULL_HANDLE) {
        r->vk.DestroyImage(r->device, *image, NULL);
    }
    if (*memory != VK_NULL_HANDLE) {
        r->vk.FreeMemory(r->device, *memory, NULL);
    }
    *view = VK_NULL_HANDLE;
    *image = VK_NULL_HANDLE;
    *memory = VK_NULL_HANDLE;
}

static void layout_change(VkSceneRenderer *r, VkImage image, VkImageLayout from, VkImageLayout to, VkAccessFlags src_access,
                          VkAccessFlags dst_access, VkPipelineStageFlags src_stage, VkPipelineStageFlags dst_stage)
{
    VkImageMemoryBarrier barrier = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                                    .srcAccessMask = src_access,
                                    .dstAccessMask = dst_access,
                                    .oldLayout = from,
                                    .newLayout = to,
                                    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                    .image = image,
                                    .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
    r->vk.CmdPipelineBarrier(r->cmd, src_stage, dst_stage, 0, 0, NULL, 0, NULL, 1, &barrier);
}

/* The pipelines ----------------------------------------------------------------------------- */

/* `which`: bit 0 translucent pixels (blended, the ID rule), bit 1 they write depth, bit 2 the
 * depth test passes on equal depth too (POLYGON_ATTR bit 14: a decal on its own surface). */
static VkPipeline pipeline_for(VkSceneRenderer *r, unsigned which)
{
    if (r->pipeline[which] != VK_NULL_HANDLE) {
        return r->pipeline[which];
    }
    int translucent = which & 1, write_depth = !translucent || (which & 2), equal = (which & 4) != 0;
    VkPipelineShaderStageCreateInfo stages[2] = {
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = r->vert, .pName = "main"},
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = r->frag, .pName = "main"},
    };
    VkVertexInputBindingDescription binding = {0, sizeof(SceneVertex), VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attributes[3] = {
        {0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(SceneVertex, pos)},
        {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(SceneVertex, color)},
        {2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(SceneVertex, st)},
    };
    VkPipelineVertexInputStateCreateInfo input = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
                                                  .vertexBindingDescriptionCount = 1,
                                                  .pVertexBindingDescriptions = &binding,
                                                  .vertexAttributeDescriptionCount = 3,
                                                  .pVertexAttributeDescriptions = attributes};
    VkPipelineInputAssemblyStateCreateInfo assembly = {.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
                                                       .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
    VkPipelineViewportStateCreateInfo viewport = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
                                                  .viewportCount = 1,
                                                  .scissorCount = 1};
    VkPipelineRasterizationStateCreateInfo raster = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
                                                     .polygonMode = VK_POLYGON_MODE_FILL,
                                                     .cullMode = VK_CULL_MODE_NONE, /* the engine has culled */
                                                     .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
                                                     .depthBiasEnable = equal ? VK_TRUE : VK_FALSE,
                                                     /* the DS's "equal" is within 0x200 of 24 bits */
                                                     .depthBiasConstantFactor = equal ? -512.0f : 0.0f,
                                                     .lineWidth = 1.0f};
    VkPipelineMultisampleStateCreateInfo multisample = {.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
                                                        .rasterizationSamples = r->samples};
    VkStencilOpState stencil = {.failOp = VK_STENCIL_OP_KEEP,
                                .passOp = VK_STENCIL_OP_REPLACE,
                                .depthFailOp = VK_STENCIL_OP_KEEP,
                                .compareOp = translucent ? VK_COMPARE_OP_NOT_EQUAL : VK_COMPARE_OP_ALWAYS,
                                .compareMask = 0xff,
                                .writeMask = 0xff};
    VkPipelineDepthStencilStateCreateInfo depth = {.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
                                                   .depthTestEnable = VK_TRUE,
                                                   .depthWriteEnable = write_depth ? VK_TRUE : VK_FALSE,
                                                   .depthCompareOp = equal ? VK_COMPARE_OP_LESS_OR_EQUAL : VK_COMPARE_OP_LESS,
                                                   .stencilTestEnable = VK_TRUE,
                                                   .front = stencil,
                                                   .back = stencil};
    VkPipelineColorBlendAttachmentState attachment = {
        .blendEnable = translucent ? VK_TRUE : VK_FALSE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT};
    VkPipelineColorBlendStateCreateInfo blend = {.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
                                                 .attachmentCount = 1,
                                                 .pAttachments = &attachment};
    VkDynamicState dynamics[3] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_STENCIL_REFERENCE};
    VkPipelineDynamicStateCreateInfo dynamic = {.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
                                                .dynamicStateCount = 3,
                                                .pDynamicStates = dynamics};
    VkGraphicsPipelineCreateInfo info = {.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
                                         .stageCount = 2,
                                         .pStages = stages,
                                         .pVertexInputState = &input,
                                         .pInputAssemblyState = &assembly,
                                         .pViewportState = &viewport,
                                         .pRasterizationState = &raster,
                                         .pMultisampleState = &multisample,
                                         .pDepthStencilState = &depth,
                                         .pColorBlendState = &blend,
                                         .pDynamicState = &dynamic,
                                         .layout = r->layout,
                                         .renderPass = r->pass};
    if (r->vk.CreateGraphicsPipelines(r->device, VK_NULL_HANDLE, 1, &info, NULL, &r->pipeline[which]) != VK_SUCCESS) {
        r->pipeline[which] = VK_NULL_HANDLE;
    }
    return r->pipeline[which];
}

/* Creation ---------------------------------------------------------------------------------- */

static int format_usable(VkSceneRenderer *r, VkFormat format, VkFormatFeatureFlags wanted)
{
    VkFormatProperties props;
    r->vk.GetPhysicalDeviceFormatProperties(r->physical, format, &props);
    return (props.optimalTilingFeatures & wanted) == wanted;
}

static const char *create(VkSceneRenderer *r, int samples)
{
    const VkApi *vk = &r->vk;
    VkPhysicalDeviceProperties props;
    vk->GetPhysicalDeviceProperties(r->physical, &props);
    VkSampleCountFlags counts = props.limits.framebufferColorSampleCounts & props.limits.framebufferDepthSampleCounts &
                                props.limits.framebufferStencilSampleCounts;
    r->samples = VK_SAMPLE_COUNT_1_BIT;
    for (int s = 8; s > 1; s >>= 1) {
        if (s <= samples && (counts & (VkSampleCountFlags) s)) {
            r->samples = (VkSampleCountFlagBits) s;
            break;
        }
    }
    r->depth_format = VK_FORMAT_D24_UNORM_S8_UINT;
    if (!format_usable(r, r->depth_format, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)) {
        r->depth_format = VK_FORMAT_D32_SFLOAT_S8_UINT;
        if (!format_usable(r, r->depth_format, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)) {
            return "no depth format with a stencil";
        }
    }
    int multi = r->samples != VK_SAMPLE_COUNT_1_BIT;
    const VkFormat color_format = VK_FORMAT_R8G8B8A8_UNORM;
    if (image_make(r, r->width, r->height, color_format, VK_SAMPLE_COUNT_1_BIT,
                   VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                   VK_IMAGE_ASPECT_COLOR_BIT, &r->output, &r->output_memory, &r->output_view) != 0 ||
        (multi && image_make(r, r->width, r->height, color_format, r->samples, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                             VK_IMAGE_ASPECT_COLOR_BIT, &r->color, &r->color_memory, &r->color_view) != 0) ||
        image_make(r, r->width, r->height, r->depth_format, r->samples, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                   VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, &r->depth, &r->depth_memory, &r->depth_view) != 0) {
        return "no memory for the picture";
    }

    /* the pass: the scene into the (multisampled) colour and depth, resolved into the output */
    VkAttachmentDescription attachments[3] = {
        {.format = color_format,
         .samples = r->samples,
         .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
         .storeOp = multi ? VK_ATTACHMENT_STORE_OP_DONT_CARE : VK_ATTACHMENT_STORE_OP_STORE,
         .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
         .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
         .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
         .finalLayout = multi ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {.format = r->depth_format,
         .samples = r->samples,
         .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
         .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
         .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
         .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
         .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
         .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL},
        {.format = color_format,
         .samples = VK_SAMPLE_COUNT_1_BIT,
         .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
         .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
         .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
         .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
         .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
         .finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
    };
    VkAttachmentReference color_ref = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depth_ref = {1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkAttachmentReference resolve_ref = {2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass = {.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    .colorAttachmentCount = 1,
                                    .pColorAttachments = &color_ref,
                                    .pResolveAttachments = multi ? &resolve_ref : NULL,
                                    .pDepthStencilAttachment = &depth_ref};
    VkSubpassDependency dependencies[2] = {
        {.srcSubpass = VK_SUBPASS_EXTERNAL,
         .dstSubpass = 0,
         .srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
         .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
         .srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT,
         .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT},
        {.srcSubpass = 0,
         .dstSubpass = VK_SUBPASS_EXTERNAL,
         .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
         .dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
         .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
         .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT},
    };
    VkRenderPassCreateInfo pass = {.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
                                   .attachmentCount = multi ? 3u : 2u,
                                   .pAttachments = attachments,
                                   .subpassCount = 1,
                                   .pSubpasses = &subpass,
                                   .dependencyCount = 2,
                                   .pDependencies = dependencies};
    if (vk->CreateRenderPass(r->device, &pass, NULL, &r->pass) != VK_SUCCESS) {
        return "no render pass";
    }
    VkImageView views[3] = {multi ? r->color_view : r->output_view, r->depth_view, r->output_view};
    VkFramebufferCreateInfo fb = {.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
                                  .renderPass = r->pass,
                                  .attachmentCount = multi ? 3u : 2u,
                                  .pAttachments = views,
                                  .width = (uint32_t) r->width,
                                  .height = (uint32_t) r->height,
                                  .layers = 1};
    if (vk->CreateFramebuffer(r->device, &fb, NULL, &r->framebuffer) != VK_SUCCESS) {
        return "no framebuffer";
    }

    /* what the shaders are given: the frame's numbers, and a texture */
    VkDescriptorSetLayoutBinding frame_binding = {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, NULL};
    VkDescriptorSetLayoutBinding texture_binding = {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, NULL};
    VkDescriptorSetLayoutCreateInfo set_info = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                                                .bindingCount = 1,
                                                .pBindings = &frame_binding};
    if (vk->CreateDescriptorSetLayout(r->device, &set_info, NULL, &r->frame_layout) != VK_SUCCESS) {
        return "no descriptor layout";
    }
    set_info.pBindings = &texture_binding;
    if (vk->CreateDescriptorSetLayout(r->device, &set_info, NULL, &r->texture_layout) != VK_SUCCESS) {
        return "no descriptor layout";
    }
    VkDescriptorSetLayout sets[2] = {r->frame_layout, r->texture_layout};
    VkPushConstantRange range = {VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PolygonConstants)};
    VkPipelineLayoutCreateInfo layout = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                                         .setLayoutCount = 2,
                                         .pSetLayouts = sets,
                                         .pushConstantRangeCount = 1,
                                         .pPushConstantRanges = &range};
    if (vk->CreatePipelineLayout(r->device, &layout, NULL, &r->layout) != VK_SUCCESS) {
        return "no pipeline layout";
    }
    VkShaderModuleCreateInfo module = {.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                                       .codeSize = sizeof(shader_scene_vert),
                                       .pCode = shader_scene_vert};
    if (vk->CreateShaderModule(r->device, &module, NULL, &r->vert) != VK_SUCCESS) {
        return "the vertex shader was refused";
    }
    module.codeSize = sizeof(shader_scene_frag);
    module.pCode = shader_scene_frag;
    if (vk->CreateShaderModule(r->device, &module, NULL, &r->frag) != VK_SUCCESS) {
        return "the fragment shader was refused";
    }
    /* the nine ways a texture's coordinates run past its edges; never filtered */
    static const VkSamplerAddressMode modes[3] = {VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_SAMPLER_ADDRESS_MODE_REPEAT,
                                                  VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT};
    for (int i = 0; i < SAMPLERS; i++) {
        VkSamplerCreateInfo sampler = {.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
                                       .magFilter = VK_FILTER_NEAREST,
                                       .minFilter = VK_FILTER_NEAREST,
                                       .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
                                       .addressModeU = modes[i % 3],
                                       .addressModeV = modes[i / 3],
                                       .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                                       .maxAnisotropy = 1.0f};
        if (vk->CreateSampler(r->device, &sampler, NULL, &r->sampler[i]) != VK_SUCCESS) {
            return "no sampler";
        }
    }
    VkDescriptorPoolSize sizes[2] = {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1},
                                     {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, (TEXTURES + 1) * SAMPLERS}};
    VkDescriptorPoolCreateInfo pool = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
                                       .maxSets = (TEXTURES + 1) * SAMPLERS + 1,
                                       .poolSizeCount = 2,
                                       .pPoolSizes = sizes};
    if (vk->CreateDescriptorPool(r->device, &pool, NULL, &r->pool) != VK_SUCCESS) {
        return "no descriptor pool";
    }
    if (buffer_make(r, &r->vertices, MAX_VERTICES * sizeof(SceneVertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT) != 0 ||
        buffer_make(r, &r->uniforms, sizeof(FrameUniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT) != 0 ||
        buffer_make(r, &r->staging, 1 << 20, VK_BUFFER_USAGE_TRANSFER_SRC_BIT) != 0 ||
        buffer_make(r, &r->readback, (VkDeviceSize) r->width * (VkDeviceSize) r->height * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT) != 0) {
        return "no memory for the buffers";
    }
    VkDescriptorSetAllocateInfo alloc = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                                         .descriptorPool = r->pool,
                                         .descriptorSetCount = 1,
                                         .pSetLayouts = &r->frame_layout};
    if (vk->AllocateDescriptorSets(r->device, &alloc, &r->frame_set) != VK_SUCCESS) {
        return "no descriptor set";
    }
    VkDescriptorBufferInfo ubo = {r->uniforms.buffer, 0, sizeof(FrameUniforms)};
    VkWriteDescriptorSet write = {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                  .dstSet = r->frame_set,
                                  .dstBinding = 0,
                                  .descriptorCount = 1,
                                  .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                  .pBufferInfo = &ubo};
    vk->UpdateDescriptorSets(r->device, 1, &write, 0, NULL);

    VkCommandPoolCreateInfo cpool = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                                     .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
                                     .queueFamilyIndex = r->family};
    VkCommandBufferAllocateInfo calloc_info = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                                               .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                                               .commandBufferCount = 1};
    VkFenceCreateInfo fence = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    if (vk->CreateCommandPool(r->device, &cpool, NULL, &r->commands) != VK_SUCCESS) {
        return "no command pool";
    }
    calloc_info.commandPool = r->commands;
    if (vk->AllocateCommandBuffers(r->device, &calloc_info, &r->cmd) != VK_SUCCESS ||
        vk->CreateFence(r->device, &fence, NULL, &r->fence) != VK_SUCCESS) {
        return "no command buffer";
    }
    return NULL;
}

VkSceneRenderer *vk_scene_create(const VkApi *api, VkPhysicalDevice physical, VkDevice device, uint32_t queue_family,
                                 VkQueue queue, int width, int height, int samples, const char **why)
{
    VkSceneRenderer *r = calloc(1, sizeof(*r));
    const char *sink;
    why = why != NULL ? why : &sink;
    if (r == NULL) {
        *why = "no memory";
        return NULL;
    }
    r->vk = *api;
    r->physical = physical;
    r->device = device;
    r->family = queue_family;
    r->queue = queue;
    r->width = width;
    r->height = height;
    *why = create(r, samples);
    if (*why != NULL) {
        vk_scene_destroy(r);
        return NULL;
    }
    return r;
}

static void texture_free(VkSceneRenderer *r, Texture *t)
{
    image_free(r, &t->image, &t->memory, &t->view);
    memset(t->set_ready, 0, sizeof(t->set_ready));
    t->width = t->height = 0;
}

void vk_scene_destroy(VkSceneRenderer *r)
{
    if (r == NULL) {
        return;
    }
    const VkApi *vk = &r->vk;
    if (r->device != VK_NULL_HANDLE) {
        vk->DeviceWaitIdle(r->device);
    }
    for (int i = 0; i < TEXTURES; i++) {
        texture_free(r, &r->textures[i]);
    }
    texture_free(r, &r->white);
    if (r->fence != VK_NULL_HANDLE) {
        vk->DestroyFence(r->device, r->fence, NULL);
    }
    if (r->commands != VK_NULL_HANDLE) {
        vk->DestroyCommandPool(r->device, r->commands, NULL);
    }
    buffer_free(r, &r->vertices);
    buffer_free(r, &r->uniforms);
    buffer_free(r, &r->staging);
    buffer_free(r, &r->readback);
    if (r->pool != VK_NULL_HANDLE) {
        vk->DestroyDescriptorPool(r->device, r->pool, NULL);
    }
    for (int i = 0; i < SAMPLERS; i++) {
        if (r->sampler[i] != VK_NULL_HANDLE) {
            vk->DestroySampler(r->device, r->sampler[i], NULL);
        }
    }
    for (int i = 0; i < PIPELINES; i++) {
        if (r->pipeline[i] != VK_NULL_HANDLE) {
            vk->DestroyPipeline(r->device, r->pipeline[i], NULL);
        }
    }
    if (r->vert != VK_NULL_HANDLE) {
        vk->DestroyShaderModule(r->device, r->vert, NULL);
    }
    if (r->frag != VK_NULL_HANDLE) {
        vk->DestroyShaderModule(r->device, r->frag, NULL);
    }
    if (r->layout != VK_NULL_HANDLE) {
        vk->DestroyPipelineLayout(r->device, r->layout, NULL);
    }
    if (r->frame_layout != VK_NULL_HANDLE) {
        vk->DestroyDescriptorSetLayout(r->device, r->frame_layout, NULL);
    }
    if (r->texture_layout != VK_NULL_HANDLE) {
        vk->DestroyDescriptorSetLayout(r->device, r->texture_layout, NULL);
    }
    if (r->framebuffer != VK_NULL_HANDLE) {
        vk->DestroyFramebuffer(r->device, r->framebuffer, NULL);
    }
    if (r->pass != VK_NULL_HANDLE) {
        vk->DestroyRenderPass(r->device, r->pass, NULL);
    }
    image_free(r, &r->color, &r->color_memory, &r->color_view);
    image_free(r, &r->depth, &r->depth_memory, &r->depth_view);
    image_free(r, &r->output, &r->output_memory, &r->output_view);
    free(r);
}

/* Textures ---------------------------------------------------------------------------------- */

/* The texture's texels put into `t` (made anew when its size changed), from the staging
 * buffer at `offset`, where the caller has put them. Commands go into r->cmd. */
static int texture_upload(VkSceneRenderer *r, Texture *t, int width, int height, VkDeviceSize offset)
{
    if (t->image == VK_NULL_HANDLE || t->width != width || t->height != height) {
        texture_free(r, t);
        if (image_make(r, width, height, VK_FORMAT_R8G8B8A8_UNORM, VK_SAMPLE_COUNT_1_BIT,
                       VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_IMAGE_ASPECT_COLOR_BIT, &t->image, &t->memory,
                       &t->view) != 0) {
            texture_free(r, t);
            return -1;
        }
        t->width = width;
        t->height = height;
    }
    layout_change(r, t->image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT,
                  VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
    VkBufferImageCopy region = {.bufferOffset = offset,
                                .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                                .imageExtent = {(uint32_t) width, (uint32_t) height, 1}};
    r->vk.CmdCopyBufferToImage(r->cmd, r->staging.buffer, t->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    layout_change(r, t->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                  VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                  VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    return 0;
}

/* The descriptor set of a texture with the sampler for a polygon's way of repeating it. */
static VkDescriptorSet texture_set(VkSceneRenderer *r, Texture *t, uint32_t texparam)
{
    int s = !(texparam & (1u << 16)) ? 0 : (texparam & (1u << 18)) ? 2 : 1;
    int v = !(texparam & (1u << 17)) ? 0 : (texparam & (1u << 19)) ? 2 : 1;
    int which = v * 3 + s;
    if (t->set[which] == VK_NULL_HANDLE) {
        VkDescriptorSetAllocateInfo alloc = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                                             .descriptorPool = r->pool,
                                             .descriptorSetCount = 1,
                                             .pSetLayouts = &r->texture_layout};
        if (r->vk.AllocateDescriptorSets(r->device, &alloc, &t->set[which]) != VK_SUCCESS) {
            t->set[which] = VK_NULL_HANDLE;
            return VK_NULL_HANDLE;
        }
        t->set_ready[which] = 0;
    }
    if (!t->set_ready[which]) {
        VkDescriptorImageInfo image = {r->sampler[which], t->view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet write = {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                      .dstSet = t->set[which],
                                      .dstBinding = 0,
                                      .descriptorCount = 1,
                                      .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                      .pImageInfo = &image};
        r->vk.UpdateDescriptorSets(r->device, 1, &write, 0, NULL);
        t->set_ready[which] = 1;
    }
    return t->set[which];
}

/* The renderer's textures for the scene's: those not held, or held with other contents, are
 * put into the staging buffer and uploaded. `of` gets each scene texture's place. */
static int textures_prepare(VkSceneRenderer *r, const Scene *scene, Texture **of)
{
    VkDeviceSize need = 4, at = 0;
    for (int i = 0; i < scene->texture_count; i++) {
        need += (VkDeviceSize) scene->textures[i].width * (VkDeviceSize) scene->textures[i].height * 4;
    }
    if (need > r->staging.size) {
        buffer_free(r, &r->staging);
        if (buffer_make(r, &r->staging, need * 2, VK_BUFFER_USAGE_TRANSFER_SRC_BIT) != 0) {
            return -1;
        }
    }
    if (r->white.image == VK_NULL_HANDLE) {
        memset(r->staging.mapped, 0xff, 4);
        if (texture_upload(r, &r->white, 1, 1, 0) != 0) {
            return -1;
        }
        at = 4;
    }
    for (int i = 0; i < scene->texture_count; i++) {
        const SceneTexture *st = &scene->textures[i];
        Texture *t = NULL, *oldest = &r->textures[0];
        for (int k = 0; k < TEXTURES; k++) {
            Texture *c = &r->textures[k];
            if (c->image != VK_NULL_HANDLE && c->texparam == st->texparam && c->pltt == st->pltt) {
                t = c;
                break;
            }
            if (c->used < oldest->used) {
                oldest = c;
            }
        }
        if (t == NULL && oldest->image != VK_NULL_HANDLE && oldest->used == r->frame) {
            return -1; /* more textures in one scene than are kept */
        }
        of[i] = t != NULL ? t : oldest;
        if (t != NULL && t->hash == st->hash && t->width == st->width && t->height == st->height) {
            t->used = r->frame;
            continue;
        }
        t = of[i];
        VkDeviceSize bytes = (VkDeviceSize) st->width * (VkDeviceSize) st->height * 4;
        memcpy((uint8_t *) r->staging.mapped + at, st->rgba, (size_t) bytes);
        memset(t->set_ready, 0, sizeof(t->set_ready));
        if (texture_upload(r, t, st->width, st->height, at) != 0) {
            return -1;
        }
        at += bytes;
        t->texparam = st->texparam;
        t->pltt = st->pltt;
        t->hash = st->hash;
        t->used = r->frame;
    }
    return 0;
}

/* Drawing ----------------------------------------------------------------------------------- */

static float six(uint32_t five)
{
    return (float) (five ? five * 2 + 1 : 0) / 63.0f; /* 5 bits to the DS's 6, as its engine does */
}

static void uniforms_fill(const Scene *scene, FrameUniforms *u)
{
    memset(u, 0, sizeof(*u));
    for (int i = 0; i < 32; i++) {
        u->toon[i][0] = six(scene->toon[i] & 31);
        u->toon[i][1] = six((scene->toon[i] >> 5) & 31);
        u->toon[i][2] = six((scene->toon[i] >> 10) & 31);
        u->toon[i][3] = 1.0f;
    }
    u->fog_color[0] = six(scene->fog_color & 31);
    u->fog_color[1] = six((scene->fog_color >> 5) & 31);
    u->fog_color[2] = six((scene->fog_color >> 10) & 31);
    u->fog_color[3] = (float) ((scene->fog_color >> 16) & 31) / 31.0f;
    /* the 32 densities with the first and last repeated at the ends, as the engine reads them */
    for (int i = 0; i < 34; i++) {
        int at = i == 0 ? 0 : i == 33 ? 31 : i - 1;
        u->fog_table[i] = (float) scene->fog_table[at] / 128.0f;
    }
    u->fog_offset = (float) scene->fog_offset;
    u->fog_scale = (float) (1u << scene->fog_shift) / 4.0f / 131072.0f;
    u->alpha_ref = (float) scene->alpha_ref;
}

int vk_scene_draw(VkSceneRenderer *r, const Scene *scene)
{
    const VkApi *vk = &r->vk;
    static Texture *texture_of[4096];
    r->frame++;
    r->read_valid = 0;
    if (scene->texture_count > 4096) {
        return -1;
    }
    VkCommandBufferBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                                      .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
    if (vk->ResetCommandBuffer(r->cmd, 0) != VK_SUCCESS || vk->BeginCommandBuffer(r->cmd, &begin) != VK_SUCCESS) {
        return -1;
    }
    if (textures_prepare(r, scene, texture_of) != 0) {
        return -1;
    }
    uniforms_fill(scene, r->uniforms.mapped);

    /* the polygons as triangles: a fan each */
    SceneVertex *out = r->vertices.mapped;
    uint32_t count = 0;
    static uint32_t first_of[4096 * 2], count_of[4096 * 2];
    int polygons = scene->polygon_count < 4096 * 2 ? scene->polygon_count : 4096 * 2;
    for (int i = 0; i < polygons; i++) {
        const ScenePolygon *p = &scene->polygons[i];
        first_of[i] = count;
        count_of[i] = 0;
        if (p->count < 3 || count + (p->count - 2) * 3 > MAX_VERTICES) {
            continue;
        }
        const SceneVertex *v = &scene->vertices[p->first];
        for (uint32_t k = 1; k + 1 < p->count; k++) {
            out[count++] = v[0];
            out[count++] = v[k];
            out[count++] = v[k + 1];
        }
        count_of[i] = count - first_of[i];
    }

    VkClearValue clears[3];
    memset(clears, 0, sizeof(clears)); /* nothing drawn: alpha 0 */
    clears[1].depthStencil.depth = (float) (scene->clear_depth * 0x200 + 0x1ff) / 16777215.0f;
    clears[1].depthStencil.stencil = 0;
    VkRenderPassBeginInfo pass = {.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
                                  .renderPass = r->pass,
                                  .framebuffer = r->framebuffer,
                                  .renderArea = {{0, 0}, {(uint32_t) r->width, (uint32_t) r->height}},
                                  .clearValueCount = r->samples != VK_SAMPLE_COUNT_1_BIT ? 3u : 2u,
                                  .pClearValues = clears};
    vk->CmdBeginRenderPass(r->cmd, &pass, VK_SUBPASS_CONTENTS_INLINE);
    VkViewport viewport = {0.0f, 0.0f, (float) r->width, (float) r->height, 0.0f, 1.0f};
    VkRect2D scissor = {{0, 0}, {(uint32_t) r->width, (uint32_t) r->height}};
    vk->CmdSetViewport(r->cmd, 0, 1, &viewport);
    vk->CmdSetScissor(r->cmd, 0, 1, &scissor);
    VkDeviceSize zero = 0;
    vk->CmdBindVertexBuffers(r->cmd, 0, 1, &r->vertices.buffer, &zero);
    vk->CmdBindDescriptorSets(r->cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->layout, 0, 1, &r->frame_set, 0, NULL);

    VkPipeline bound = VK_NULL_HANDLE;
    VkDescriptorSet bound_set = VK_NULL_HANDLE;
    for (int i = 0; i < polygons; i++) {
        const ScenePolygon *p = &scene->polygons[i];
        uint32_t polyalpha = (p->attr >> 16) & 31, mode = (p->attr >> 4) & 3;
        if (count_of[i] == 0 || p->shadow_mask || p->shadow || mode == 3) {
            continue; /* shadow volumes: not yet */
        }
        Texture *t = p->texture >= 0 && p->texture < scene->texture_count ? texture_of[p->texture] : NULL;
        PolygonConstants pc;
        pc.inv_size[0] = t != NULL ? 1.0f / (float) t->width : 1.0f;
        pc.inv_size[1] = t != NULL ? 1.0f / (float) t->height : 1.0f;
        pc.alpha = polyalpha == 0 ? 1.0f : (float) polyalpha / 31.0f; /* a wire frame's lines are solid */
        uint32_t flags = (t != NULL ? F_TEXTURED : 0) | mode << 1 | (p->wbuffer ? F_W_BUFFER : 0);
        flags |= mode == 2 && (scene->disp3dcnt & 2) ? F_HIGHLIGHT : 0;
        flags |= (p->attr & 0x8000) && (scene->disp3dcnt & 0x80) ? F_FOG : 0;
        flags |= (scene->disp3dcnt & 0x40) ? F_FOG_ALPHA_ONLY : 0;
        VkDescriptorSet set = texture_set(r, t != NULL ? t : &r->white, t != NULL ? p->texparam : 0);
        if (set == VK_NULL_HANDLE) {
            continue;
        }
        if (set != bound_set) {
            vk->CmdBindDescriptorSets(r->cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->layout, 1, 1, &set, 0, NULL);
            bound_set = set;
        }
        unsigned equal = (p->attr & (1u << 14)) ? 4 : 0;
        /* the opaque pixels (all of them, for a polygon that is not translucent), then the
           translucent ones */
        for (int translucent = 0; translucent < 2; translucent++) {
            if (translucent ? !p->translucent : (p->translucent && pc.alpha < 1.0f)) {
                continue; /* it has no pixels of this kind */
            }
            unsigned which = translucent ? (1u | ((p->attr & (1u << 11)) ? 2u : 0u) | equal) : equal;
            VkPipeline pipeline = pipeline_for(r, which);
            if (pipeline == VK_NULL_HANDLE) {
                continue;
            }
            if (pipeline != bound) {
                vk->CmdBindPipeline(r->cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
                bound = pipeline;
            }
            pc.flags = flags | (!p->translucent ? 0 : translucent ? F_ONLY_TRANSLUCENT : F_ONLY_OPAQUE);
            vk->CmdSetStencilReference(r->cmd, VK_STENCIL_FACE_FRONT_AND_BACK,
                                       translucent ? STENCIL_TRANSLUCENT | ((p->attr >> 24) & 0x3f) : 0);
            vk->CmdPushConstants(r->cmd, r->layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
            vk->CmdDraw(r->cmd, count_of[i], 1, first_of[i], 0);
        }
    }
    vk->CmdEndRenderPass(r->cmd);
    if (vk->EndCommandBuffer(r->cmd) != VK_SUCCESS) {
        return -1;
    }
    VkSubmitInfo submit = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &r->cmd};
    vk->ResetFences(r->device, 1, &r->fence);
    if (vk->QueueSubmit(r->queue, 1, &submit, r->fence) != VK_SUCCESS ||
        vk->WaitForFences(r->device, 1, &r->fence, VK_TRUE, 5000000000ull) != VK_SUCCESS) {
        return -1;
    }
    return 0;
}

const uint8_t *vk_scene_read(VkSceneRenderer *r)
{
    const VkApi *vk = &r->vk;
    if (r->read_valid) {
        return r->readback.mapped;
    }
    VkCommandBufferBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                                      .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
    if (vk->ResetCommandBuffer(r->cmd, 0) != VK_SUCCESS || vk->BeginCommandBuffer(r->cmd, &begin) != VK_SUCCESS) {
        return NULL;
    }
    layout_change(r, r->output, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_SHADER_READ_BIT,
                  VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
    VkBufferImageCopy region = {.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                                .imageExtent = {(uint32_t) r->width, (uint32_t) r->height, 1}};
    vk->CmdCopyImageToBuffer(r->cmd, r->output, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, r->readback.buffer, 1, &region);
    layout_change(r, r->output, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_TRANSFER_READ_BIT,
                  VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    VkSubmitInfo submit = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &r->cmd};
    vk->ResetFences(r->device, 1, &r->fence);
    if (vk->EndCommandBuffer(r->cmd) != VK_SUCCESS || vk->QueueSubmit(r->queue, 1, &submit, r->fence) != VK_SUCCESS ||
        vk->WaitForFences(r->device, 1, &r->fence, VK_TRUE, 5000000000ull) != VK_SUCCESS) {
        return NULL;
    }
    r->read_valid = 1;
    return r->readback.mapped;
}

VkImageView vk_scene_view(VkSceneRenderer *r)
{
    return r->output_view;
}
