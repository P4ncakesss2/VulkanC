#include "renderer.h"
#include "logger.h"
#include <string.h>

static void transition_images_for_render(VkCommandBuffer cmd,
                                         VkWindow*        window,
                                         uint32_t         imageIndex)
{
    VkImageMemoryBarrier2 barriers[3] = {
        {
            .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask        = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            .srcAccessMask       = 0,
            .dstStageMask        = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstAccessMask       = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            .oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout           = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image               = window->swapChainImages[imageIndex],
            .subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
        },
        {
            .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask        = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            .srcAccessMask       = 0,
            .dstStageMask        = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstAccessMask       = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            .oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout           = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image               = window->msaaColorImage,
            .subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
        },
        {
            .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask        = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
            .srcAccessMask       = 0,
            .dstStageMask        = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
            .dstAccessMask       = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                                   VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            .oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout           = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image               = window->depthImage,
            .subresourceRange    = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 },
        },
    };
    VkDependencyInfo dep = {
        .sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 3,
        .pImageMemoryBarriers    = barriers,
    };
    vkCmdPipelineBarrier2(cmd, &dep);
}

static void transition_image_for_present(VkCommandBuffer cmd,
                                          VkImage          image)
{
    VkImageMemoryBarrier2 barrier = {
        .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask        = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        .srcAccessMask       = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        .dstStageMask        = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
        .dstAccessMask       = 0,
        .oldLayout           = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .newLayout           = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image               = image,
        .subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
    };
    VkDependencyInfo dep = {
        .sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers    = &barrier,
    };
    vkCmdPipelineBarrier2(cmd, &dep);
}

VulkanResult vkRendererCreate(VkContext* ctx, VkWindow* window, VkRenderer* out) {
    if (!ctx || !window || !out)
        return (VulkanResult){ .status = VULKAN_ERROR_INSTANCE_CREATION_FAILED,
                               .vk_result = VK_ERROR_INITIALIZATION_FAILED };
    memset(out, 0, sizeof(VkRenderer));
    out->ctx    = ctx;
    out->window = window;
    return (VulkanResult){ .status = VULKAN_SUCCESS, .vk_result = VK_SUCCESS };
}

VulkanResult vkRendererAddPass(VkRenderer* r, RenderPass* pass) {
    if (r->passCount >= RENDERER_MAX_PASSES) {
        LOG_ERROR("vkRendererAddPass: pass limit (%d) reached", RENDERER_MAX_PASSES);
        return (VulkanResult){ .status = VULKAN_ERROR_INSTANCE_CREATION_FAILED,
                               .vk_result = VK_ERROR_INITIALIZATION_FAILED };
    }

    VulkanResult res = pass->iface.init(r, pass, r->ctx, r->window);
    if (res.status != VULKAN_SUCCESS) {
        LOG_ERROR("vkRendererAddPass: init failed for pass '%s'", pass->name ? pass->name : "?");
        pass->iface.destroy(pass, r->ctx);
        return res;
    }

    r->passes[r->passCount++] = pass;
    return (VulkanResult){ .status = VULKAN_SUCCESS, .vk_result = VK_SUCCESS };
}

void vkRendererRemovePass(VkRenderer* r, RenderPass* pass) {
    for (uint32_t i = 0; i < r->passCount; i++) {
        if (r->passes[i] == pass) {
            if (pass->iface.destroy)
                pass->iface.destroy(pass, r->ctx);
            for (uint32_t j = i + 1; j < r->passCount; j++)
                r->passes[j - 1] = r->passes[j];
            r->passCount--;
            return;
        }
    }
    LOG_WARN("vkRendererRemovePass: pass '%s' not found", pass->name ? pass->name : "?");
}

void vkRendererDestroy(VkRenderer* r) {
    if (!r || !r->ctx) return;
    for (int i = (int)r->passCount - 1; i >= 0; i--) {
        RenderPass* pass = r->passes[i];
        if (pass && pass->iface.destroy)
            pass->iface.destroy(pass, r->ctx);
    }
    memset(r, 0, sizeof(VkRenderer));
}

bool vkRendererNeedsResize(const VkRenderer* r) {
    return r->needsSwapchainRebuild;
}

VulkanResult vkRendererResize(VkRenderer* r) {
    VulkanResult res = vkWindowRecreateSwapchain(r->ctx, r->window);
    if (res.status != VULKAN_SUCCESS) return res;

    for (uint32_t i = 0; i < r->passCount; i++) {
        RenderPass* pass = r->passes[i];
        if (pass && pass->iface.on_resize) {
            res = pass->iface.on_resize(pass, r->ctx, r->window);
            if (res.status != VULKAN_SUCCESS) {
                LOG_ERROR("vkRendererResize: on_resize failed for pass '%s'",
                          pass->name ? pass->name : "?");
                return res;
            }
        }
    }
    return (VulkanResult){ .status = VULKAN_SUCCESS, .vk_result = VK_SUCCESS };
}

VulkanResult vkRendererBeginFrame(VkRenderer* r) {
    if (r->frameActive) {
        LOG_WARN("vkRendererBeginFrame: frame already active");
        return (VulkanResult){ .status = VULKAN_SUCCESS, .vk_result = VK_SUCCESS };
    }

    r->needsSwapchainRebuild = false;
    VkWindow*    window    = r->window;
    VkContext*   ctx       = r->ctx;
    uint32_t     fi        = window->frameIndex;
    VkFrameData* frameData = &window->frames[fi];

    VkResult res = vkWaitForFences(ctx->logicalDevice, 1, &frameData->renderFence,
                                   VK_TRUE, UINT64_MAX);
    if (res != VK_SUCCESS)
        return (VulkanResult){ .status = VULKAN_ERROR_FENCE_WAIT_FAILED, .vk_result = res };

    uint32_t imageIndex;
    res = vkAcquireNextImageKHR(ctx->logicalDevice, window->swapChain, UINT64_MAX,
                                 frameData->presentSemaphore, VK_NULL_HANDLE, &imageIndex);
    if (res == VK_ERROR_OUT_OF_DATE_KHR) {
        r->needsSwapchainRebuild = true;
        return (VulkanResult){ .status = VULKAN_STATUS_SWAPCHAIN_OUTDATED, .vk_result = res };
    }
    if (res != VK_SUCCESS && res != VK_SUBOPTIMAL_KHR)
        return (VulkanResult){ .status = VULKAN_ERROR_SWAPCHAIN_NEXT_IMAGE_FAILED, .vk_result = res };

    vkResetFences(ctx->logicalDevice, 1, &frameData->renderFence);
    r->frameIndex  = fi;
    r->imageIndex  = imageIndex;
    r->frameActive = true;

    VkImageData* imageData = &window->imageData[imageIndex];
    if (imageData->commandBufferRecorded) {
        vkResetCommandPool(ctx->logicalDevice, imageData->graphicsPool, 0);
        imageData->commandBufferRecorded = false;
    }

    VkCommandBufferBeginInfo beginInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    };
    res = vkBeginCommandBuffer(imageData->graphicsCommandBuffer, &beginInfo);
    if (res != VK_SUCCESS)
        return (VulkanResult){ .status = VULKAN_ERROR_COMMAND_BUFFER_FAILED_BEGIN, .vk_result = res };

    transition_images_for_render(imageData->graphicsCommandBuffer, window, imageIndex);

    return (VulkanResult){ .status = VULKAN_SUCCESS, .vk_result = VK_SUCCESS };
}

void vkRendererExecutePasses(VkRenderer* r, float dt, float totalTime) {
    if (!r->frameActive) {
        LOG_WARN("vkRendererExecutePasses: no active frame");
        return;
    }

    VkImageData* imageData = &r->window->imageData[r->imageIndex];

    RenderFrameContext fc = {
        .ctx        = r->ctx,
        .window     = r->window,
        .cmd        = imageData->graphicsCommandBuffer,
        .frameIndex = r->frameIndex,
        .imageIndex = r->imageIndex,
        .deltaTime  = dt,
        .totalTime  = totalTime,
    };

    for (uint32_t i = 0; i < r->passCount; i++) {
        RenderPass* pass = r->passes[i];
        if (!pass || !pass->enabled) continue;
        pass->iface.execute(pass, &fc);
    }
}

VulkanResult vkRendererEndFrame(VkRenderer* r) {
    if (!r->frameActive) {
        LOG_WARN("vkRendererEndFrame: no active frame");
        return (VulkanResult){ .status = VULKAN_SUCCESS, .vk_result = VK_SUCCESS };
    }

    VkWindow*    window    = r->window;
    VkContext*   ctx       = r->ctx;
    uint32_t     fi        = r->frameIndex;
    uint32_t     ii        = r->imageIndex;
    VkImageData* imageData = &window->imageData[ii];
    VkFrameData* frameData = &window->frames[fi];

    transition_image_for_present(imageData->graphicsCommandBuffer,
                                  window->swapChainImages[ii]);

    VkResult res = vkEndCommandBuffer(imageData->graphicsCommandBuffer);
    if (res != VK_SUCCESS) {
        r->frameActive = false;
        return (VulkanResult){ .status = VULKAN_ERROR_COMMAND_BUFFER_FAILED_END, .vk_result = res };
    }
    imageData->commandBufferRecorded = true;

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit = {
        .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount   = 1,
        .pWaitSemaphores      = &frameData->presentSemaphore,
        .pWaitDstStageMask    = &waitStage,
        .commandBufferCount   = 1,
        .pCommandBuffers      = &imageData->graphicsCommandBuffer,
        .signalSemaphoreCount = 1,
        .pSignalSemaphores    = &window->renderSemaphores[ii],
    };
    res = vkQueueSubmit(ctx->queues.graphics, 1, &submit, frameData->renderFence);
    if (res != VK_SUCCESS) {
        r->frameActive = false;
        return (VulkanResult){ .status = VULKAN_ERROR_QUEUE_SUBMIT_FAILED, .vk_result = res };
    }

    VkPresentInfoKHR presentInfo = {
        .sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores    = &window->renderSemaphores[ii],
        .swapchainCount     = 1,
        .pSwapchains        = &window->swapChain,
        .pImageIndices      = &ii,
    };
    res = vkQueuePresentKHR(ctx->queues.graphics, &presentInfo);

    r->frameActive     = false;
    window->frameIndex = (fi + 1) % MAX_FRAMES_IN_FLIGHT;

    if (res == VK_ERROR_OUT_OF_DATE_KHR || res == VK_SUBOPTIMAL_KHR ||
        window->framebufferResized)
    {
        window->framebufferResized = false;
        r->needsSwapchainRebuild   = true;
        return (VulkanResult){ .status = VULKAN_SUCCESS, .vk_result = VK_SUCCESS };
    }
    if (res != VK_SUCCESS)
        return (VulkanResult){ .status = VULKAN_ERROR_QUEUE_PRESENT_FAILED, .vk_result = res };

    return (VulkanResult){ .status = VULKAN_SUCCESS, .vk_result = VK_SUCCESS };
}

void vkRendererMakeSwapchainRenderingInfo(
    const VkWindow*            window,
    uint32_t                   imageIndex,
    const float                clearColor[4],
    VkRenderingAttachmentInfo* colorAttachment,
    VkRenderingAttachmentInfo* depthAttachment,
    VkRenderingInfo*           out)
{
    float cr = clearColor ? clearColor[0] : 0.1f;
    float cg = clearColor ? clearColor[1] : 0.1f;
    float cb = clearColor ? clearColor[2] : 0.1f;
    float ca = clearColor ? clearColor[3] : 1.0f;

    *colorAttachment = (VkRenderingAttachmentInfo){
        .sType              = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView          = window->msaaColorImageView,
        .imageLayout        = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp             = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp            = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .resolveMode        = VK_RESOLVE_MODE_AVERAGE_BIT,
        .resolveImageView   = window->swapChainImageViews[imageIndex],
        .resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .clearValue         = { .color = {{ cr, cg, cb, ca }} },
    };
    *depthAttachment = (VkRenderingAttachmentInfo){
        .sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView   = window->depthImageView,
        .imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        .loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp     = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .clearValue  = { .depthStencil = { 1.0f, 0 } },
    };
    *out = (VkRenderingInfo){
        .sType                = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea           = { .offset = {0, 0}, .extent = window->swapChainExtent },
        .layerCount           = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments    = colorAttachment,
        .pDepthAttachment     = depthAttachment,
    };
}