#ifndef RENDER_PASS_H
#define RENDER_PASS_H

#include "vulkan_ctx.h"
#include "window.h"
#include "buffer.h"
#include <stdint.h>
#include <stdbool.h>

typedef struct RenderFrameContext {
    VkContext*      ctx;
    VkWindow*       window;
    VkCommandBuffer cmd;
    uint32_t        frameIndex;
    uint32_t        imageIndex;
    float           deltaTime;
    float           totalTime;
} RenderFrameContext;

typedef struct RenderPass RenderPass;
typedef struct VkRenderer VkRenderer;

typedef struct RenderPassInterface {
    VulkanResult (*init)   (VkRenderer* renderer, RenderPass* pass, VkContext* ctx, VkWindow* window);
    void         (*destroy)(RenderPass* pass, VkContext* ctx);
    void         (*execute)(RenderPass* pass, const RenderFrameContext* fc);
    VulkanResult (*on_resize)(RenderPass* pass, VkContext* ctx, VkWindow* window);
} RenderPassInterface;


struct RenderPass {
    const char*          name;
    RenderPassInterface  iface;
    void*                pdata;
    bool                 enabled;
};

static inline RenderPass renderPassMake(const char*         name,
                                        RenderPassInterface iface,
                                        void*               pdata)
{
    return (RenderPass){ .name = name, .iface = iface, .pdata = pdata, .enabled = true };
}

#endif