#ifndef RENDERER_H
#define RENDERER_H

#include "vulkan_ctx.h"
#include "window.h"
#include "buffer.h"
#include "render_pass.h"
#include <stdint.h>
#include <stdbool.h>

typedef enum VkPassType {
    PASS_TYPE_GEOMETRY = 0,
    PASS_TYPE_COUNT,
} VkPassType;

#define RENDERER_MAX_PASSES 32

typedef struct VkRenderer {
    VkContext* ctx;
    VkWindow*  window;

    RenderPass* passes[RENDERER_MAX_PASSES];
    uint32_t    passCount;

    uint32_t frameIndex;
    uint32_t imageIndex;
    bool     frameActive;
    bool     needsSwapchainRebuild;
} VkRenderer;


VulkanResult vkRendererCreate(VkContext* ctx, VkWindow* window, VkRenderer* out);

VulkanResult vkRendererAddPass(VkRenderer* r, RenderPass* pass);

void vkRendererRemovePass(VkRenderer* r, RenderPass* pass);
void vkRendererDestroy(VkRenderer* r);

VulkanResult vkRendererBeginFrame(VkRenderer* r);
void         vkRendererExecutePasses(VkRenderer* r, float dt, float totalTime);
VulkanResult vkRendererEndFrame(VkRenderer* r);
VulkanResult vkRendererResize(VkRenderer* r);

bool vkRendererNeedsResize(const VkRenderer* r);

void vkRendererMakeSwapchainRenderingInfo(
    const VkWindow*             window,
    uint32_t                    imageIndex,
    const float                 clearColor[4],
    VkRenderingAttachmentInfo*  colorAttachment,
    VkRenderingAttachmentInfo*  depthAttachment,
    VkRenderingInfo*            out);

#endif 