#include "geometry_pass.h"
#include "cull_pass.h"
#include "pipeline_cache.h"
#include "renderer.h"
#include "logger.h"
#include <string.h>

static VulkanResult geo_init(VkRenderer* renderer, RenderPass* pass, VkContext* ctx, VkWindow* window) {
    GeometryPassData* d   = (GeometryPassData*)pass->pdata;
    VkDevice          dev = ctx->logicalDevice;

    d->colorFormat = window->swapChainSurfaceFormat.format;
    d->depthFormat = VK_FORMAT_D32_SFLOAT;

    VkDescriptorSetLayoutBinding uboBinding = {
        .binding         = 0,
        .descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .descriptorCount = 1,
        .stageFlags      = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
    };
    VkDescriptorSetLayoutCreateInfo lci = {
        .sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1,
        .pBindings    = &uboBinding,
    };
    VkResult vr = vkCreateDescriptorSetLayout(dev, &lci, NULL, &d->passSetLayout);
    if (vr != VK_SUCCESS)
        return (VulkanResult){ .status = VULKAN_ERROR_DESCRIPTOR_SET_LAYOUT_CREATION_FAILED, .vk_result = vr };

    VkDescriptorPoolSize ps = {
        .type            = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .descriptorCount = MAX_FRAMES_IN_FLIGHT,
    };
    VkDescriptorPoolCreateInfo pci = {
        .sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets       = MAX_FRAMES_IN_FLIGHT,
        .poolSizeCount = 1,
        .pPoolSizes    = &ps,
    };
    vr = vkCreateDescriptorPool(dev, &pci, NULL, &d->passPool);
    if (vr != VK_SUCCESS)
        return (VulkanResult){ .status = VULKAN_ERROR_DESCRIPTOR_POOL_CREATION_FAILED, .vk_result = vr };

    VkDescriptorSetLayout layouts[MAX_FRAMES_IN_FLIGHT];
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) layouts[i] = d->passSetLayout;
    VkDescriptorSetAllocateInfo dsai = {
        .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool     = d->passPool,
        .descriptorSetCount = MAX_FRAMES_IN_FLIGHT,
        .pSetLayouts        = layouts,
    };
    vr = vkAllocateDescriptorSets(dev, &dsai, d->passSets);
    if (vr != VK_SUCCESS)
        return (VulkanResult){ .status = VULKAN_ERROR_DESCRIPTOR_SET_ALLOCATION_FAILED, .vk_result = vr };

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VulkanResult res = vkBufferCreate(ctx, sizeof(GeometryPassUBO),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
            &d->passUboBuffers[i]);
        if (res.status != VULKAN_SUCCESS) return res;

        VkDescriptorBufferInfo bi = {
            .buffer = d->passUboBuffers[i].buffer,
            .offset = 0,
            .range  = sizeof(GeometryPassUBO),
        };
        VkWriteDescriptorSet w = {
            .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet          = d->passSets[i],
            .dstBinding      = 0,
            .descriptorCount = 1,
            .descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .pBufferInfo     = &bi,
        };
        vkUpdateDescriptorSets(dev, 1, &w, 0, NULL);
    }

    (void)renderer;
    return (VulkanResult){ .status = VULKAN_SUCCESS, .vk_result = VK_SUCCESS };
}

static void geo_destroy(RenderPass* pass, VkContext* ctx) {
    GeometryPassData* d   = (GeometryPassData*)pass->pdata;
    VkDevice          dev = ctx->logicalDevice;

    pipelineCacheDestroy(&d->pipelines, ctx);

    if (d->passPool      != VK_NULL_HANDLE) vkDestroyDescriptorPool(dev, d->passPool, NULL);
    if (d->passSetLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(dev, d->passSetLayout, NULL);
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        if (d->passUboBuffers[i].buffer) vkBufferDestroy(ctx, &d->passUboBuffers[i]);

    memset(d, 0, sizeof(GeometryPassData));
}

static VulkanResult geo_on_resize(RenderPass* pass, VkContext* ctx, VkWindow* window) {
    (void)ctx;
    GeometryPassData* d = (GeometryPassData*)pass->pdata;
    d->colorFormat = window->swapChainSurfaceFormat.format;
    return (VulkanResult){ .status = VULKAN_SUCCESS, .vk_result = VK_SUCCESS };
}

static void geo_execute(RenderPass* pass, const RenderFrameContext* fc) {
    GeometryPassData* d   = (GeometryPassData*)pass->pdata;
    VkCommandBuffer   cmd = fc->cmd;
    uint32_t          fi  = fc->frameIndex;

    memcpy(d->passUboBuffers[fi].mappedData, &d->ubo, sizeof(GeometryPassUBO));

    VkRenderingAttachmentInfo colorAttachment, depthAttachment;
    VkRenderingInfo           renderingInfo;
    vkRendererMakeSwapchainRenderingInfo(fc->window, fc->imageIndex, NULL,
                                         &colorAttachment, &depthAttachment, &renderingInfo);
    vkCmdBeginRendering(cmd, &renderingInfo);

    VkViewport vp = {
        .x = 0, .y = 0,
        .width    = (float)fc->window->swapChainExtent.width,
        .height   = (float)fc->window->swapChainExtent.height,
        .minDepth = 0.0f, .maxDepth = 1.0f,
    };
    vkCmdSetViewport(cmd, 0, 1, &vp);
    VkRect2D scissor = { .offset = {0, 0}, .extent = fc->window->swapChainExtent };
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    CullPassData* cull = d->cullData;

    PipelineCacheConfig cfg = {
        .passType        = PASS_TYPE_GEOMETRY,
        .globalSetLayout = fc->ctx->globalSetLayout,
        .passSetLayout   = d->passSetLayout,
        .colorFormat     = d->colorFormat,
        .depthFormat     = d->depthFormat,
    };

    for (uint32_t b = 0; b < d->batchCount; b++) {
        GeometryBatch* batch = &d->batches[b];
        RenderObject*  o     = batch->representativeObject;

        PipelineEntry* e = pipelineCacheGet(&d->pipelines, fc->ctx, &cfg, o->material);
        if (!e) continue;

        if (e->hasMatSet)
            memcpy(e->matUbo[fi].mappedData, o->material->uboParams.data, o->material->uboParams.size);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, e->pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, e->layout,
                                0, 1, &fc->ctx->globalDescriptorSets[fi], 0, NULL);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, e->layout,
                                1, 1, &d->passSets[fi], 0, NULL);
        if (e->hasMatSet)
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, e->layout,
                                    2, 1, &e->matSets[fi], 0, NULL);

        if (o->mesh) {
            vkMeshBind(o->mesh, cmd);
            vkCmdDrawIndexedIndirectCount(cmd,
                cull->drawCommandBuffers[fi].buffer,
                batch->drawCommandOffset * sizeof(VkDrawIndexedIndirectCommand),
                cull->drawCountBuffers[fi].buffer,
                batch->drawCountOffset,
                batch->maxDrawCount,
                sizeof(VkDrawIndexedIndirectCommand));
        } else {
            // Skybox or other full-screen effect – draw without geometry
            vkCmdDraw(cmd, 3, 1, 0, 0);
        }
    }

    vkCmdEndRendering(cmd);
}

RenderPass geometryPassCreate(GeometryPassData* pdata) {
    return renderPassMake("geometry", (RenderPassInterface){
        .init      = geo_init,
        .destroy   = geo_destroy,
        .execute   = geo_execute,
        .on_resize = geo_on_resize,
    }, pdata);
}