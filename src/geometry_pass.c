#include "geometry_pass.h"
#include "renderer.h"
#include "logger.h"
#include <string.h>

static VulkanResult geo_init(VkRenderer* renderer, RenderPass* pass, VkContext* ctx, VkWindow* window) {
    GeometryPassData* d   = (GeometryPassData*)pass->pdata;
    VkDevice          dev = ctx->logicalDevice;

    d->colorFormat = window->swapChainSurfaceFormat.format;
    d->depthFormat = VK_FORMAT_D32_SFLOAT;
    d->setLayout = renderer->setLayouts[PASS_TYPE_GEOMETRY];

    VkDescriptorPoolSize poolSize = {
        .type            = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .descriptorCount = MAX_FRAMES_IN_FLIGHT,
    };
    VkDescriptorPoolCreateInfo poolCI = {
        .sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets       = MAX_FRAMES_IN_FLIGHT,
        .poolSizeCount = 1,
        .pPoolSizes    = &poolSize,
    };
    VkResult vr = vkCreateDescriptorPool(dev, &poolCI, NULL, &d->pool);
    if (vr != VK_SUCCESS)
        return (VulkanResult){ .status = VULKAN_ERROR_DESCRIPTOR_POOL_CREATION_FAILED, .vk_result = vr };

    VkDescriptorSetLayout layouts[MAX_FRAMES_IN_FLIGHT];
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) layouts[i] = d->setLayout;

    VkDescriptorSetAllocateInfo dsAI = {
        .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool     = d->pool,
        .descriptorSetCount = MAX_FRAMES_IN_FLIGHT,
        .pSetLayouts        = layouts,
    };
    vr = vkAllocateDescriptorSets(dev, &dsAI, d->sets);
    if (vr != VK_SUCCESS)
        return (VulkanResult){ .status = VULKAN_ERROR_DESCRIPTOR_SET_ALLOCATION_FAILED, .vk_result = vr };

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VulkanResult res = vkBufferCreate(
            ctx, sizeof(GeometryPassUBO),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
            &d->uboBuffers[i]);
        if (res.status != VULKAN_SUCCESS) return res;

        VkDescriptorBufferInfo bufInfo = {
            .buffer = d->uboBuffers[i].buffer,
            .offset = 0,
            .range  = sizeof(GeometryPassUBO),
        };
        VkWriteDescriptorSet w = {
            .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet          = d->sets[i],
            .dstBinding      = 0,
            .descriptorCount = 1,
            .descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .pBufferInfo     = &bufInfo,
        };
        vkUpdateDescriptorSets(dev, 1, &w, 0, NULL);
    }

    return (VulkanResult){ .status = VULKAN_SUCCESS, .vk_result = VK_SUCCESS };
}

static void geo_destroy(RenderPass* pass, VkContext* ctx) {
    GeometryPassData* d   = (GeometryPassData*)pass->pdata;
    VkDevice          dev = ctx->logicalDevice;

    if (d->pool      != VK_NULL_HANDLE) vkDestroyDescriptorPool(dev, d->pool, NULL);
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        if (d->uboBuffers[i].buffer) vkBufferDestroy(ctx, &d->uboBuffers[i]);
    }
    memset(d, 0, sizeof(GeometryPassData));
}


static VulkanResult geo_on_resize(RenderPass* pass, VkContext* ctx, VkWindow* window) {
    (void)ctx;
    GeometryPassData* d = (GeometryPassData*)pass->pdata;
    d->colorFormat = window->swapChainSurfaceFormat.format;
    return (VulkanResult){ .status = VULKAN_SUCCESS, .vk_result = VK_SUCCESS };
}

static void draw_objects_linear(VkContext* ctx, GeometryPassData*       d,
                                 VkCommandBuffer         cmd,
                                 uint32_t                fi,
                                 VkDescriptorSet         passSet)
{
    VkMaterial* boundMat  = NULL;
    VkMesh*     boundMesh = NULL;

    for (uint32_t i = 0; i < d->objectCount; i++) {
        RenderObject* o = &d->objects[i];

        if (!vkMaterialSupportsPass(o->material, PASS_TYPE_GEOMETRY)) continue;

        if (o->material != boundMat) {
            vkMaterialFlush(o->material, ctx->logicalDevice, fi);
            vkMaterialBindPipeline(cmd, o->material, PASS_TYPE_GEOMETRY);

            VkPipelineLayout layout = o->material->pipelineLayouts[PASS_TYPE_GEOMETRY];

            // set 0 — global (bindless textures, global UBO, SSBO)
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                     layout, 0, 1,
                                     &ctx->globalDescriptorSets[fi], 0, NULL);

            // set 1 — geometry pass UBO (proj/view/invProj)
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                     layout, 1, 1, &passSet, 0, NULL);

            // set 2 — per-material (optional, SPIRV-Reflect driven)
            if (o->material->hasSet) {
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                         layout, 2, 1, &o->material->sets[fi], 0, NULL);
            }

            boundMat  = o->material;
            boundMesh = NULL;
        }

        if (o->mesh) {
            if (o->mesh != boundMesh) {
                vkMeshBind(o->mesh, cmd);
                boundMesh = o->mesh;
            }
            vkMeshDraw(o->mesh, cmd, i);
        } else {
            vkCmdDraw(cmd, 3, 1, 0, i);
        }
    }
}

static void geo_execute(RenderPass* pass, const RenderFrameContext* fc) {
    GeometryPassData* d   = (GeometryPassData*)pass->pdata;
    VkCommandBuffer   cmd = fc->cmd;
    uint32_t          fi  = fc->frameIndex;

    memcpy(d->uboBuffers[fi].mappedData, &d->ubo, sizeof(GeometryPassUBO));

    VkRenderingAttachmentInfo colorAttachment, depthAttachment;
    VkRenderingInfo           renderingInfo;
    vkRendererMakeSwapchainRenderingInfo(fc->window, fc->imageIndex,
                                         NULL,
                                         &colorAttachment, &depthAttachment,
                                         &renderingInfo);

    vkCmdBeginRendering(cmd, &renderingInfo);

    VkViewport vp = {
        .x        = 0.0f, .y        = 0.0f,
        .width    = (float)fc->window->swapChainExtent.width,
        .height   = (float)fc->window->swapChainExtent.height,
        .minDepth = 0.0f, .maxDepth = 1.0f,
    };
    vkCmdSetViewport(cmd, 0, 1, &vp);
    VkRect2D scissor = { .offset = {0, 0}, .extent = fc->window->swapChainExtent };
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    draw_objects_linear(fc->ctx, d, cmd, fi, d->sets[fi]);
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