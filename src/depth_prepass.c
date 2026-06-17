#include "depth_prepass.h"
#include "pipeline_cache.h"
#include "renderer.h"
#include "logger.h"
#include <string.h>

static VulkanResult create_depth_image(VkContext* ctx, VkExtent2D extent,
                                        VkFormat format, VkImage* outImage,
                                        VkDeviceMemory* outMemory, VkImageView* outView) {
    VkImageCreateInfo ici = {
        .sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType     = VK_IMAGE_TYPE_2D,
        .format        = format,
        .extent        = { extent.width, extent.height, 1 },
        .mipLevels     = 1,
        .arrayLayers   = 1,
        .samples       = VK_SAMPLE_COUNT_1_BIT,
        .tiling        = VK_IMAGE_TILING_OPTIMAL,
        .usage         = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .sharingMode   = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    VkResult vr = vkCreateImage(ctx->logicalDevice, &ici, NULL, outImage);
    if (vr != VK_SUCCESS)
        return (VulkanResult){ .status = VULKAN_ERROR_IMAGE_CREATION_FAILED, .vk_result = vr };

    VkMemoryRequirements memReq;
    vkGetImageMemoryRequirements(ctx->logicalDevice, *outImage, &memReq);

    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(ctx->physicalDevice, &memProps);

    uint32_t memTypeIndex = UINT32_MAX;
    for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
        if ((memReq.memoryTypeBits & (1u << i)) &&
            (memProps.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
            memTypeIndex = i;
            break;
        }
    }
    if (memTypeIndex == UINT32_MAX) {
        vkDestroyImage(ctx->logicalDevice, *outImage, NULL);
        return (VulkanResult){ .status = VULKAN_ERROR_MEMORY_ALLOCATION_FAILED };
    }

    VkMemoryAllocateInfo mai = {
        .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize  = memReq.size,
        .memoryTypeIndex = memTypeIndex,
    };
    vr = vkAllocateMemory(ctx->logicalDevice, &mai, NULL, outMemory);
    if (vr != VK_SUCCESS) {
        vkDestroyImage(ctx->logicalDevice, *outImage, NULL);
        return (VulkanResult){ .status = VULKAN_ERROR_MEMORY_ALLOCATION_FAILED, .vk_result = vr };
    }
    vkBindImageMemory(ctx->logicalDevice, *outImage, *outMemory, 0);

    VkImageViewCreateInfo ivci = {
        .sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image    = *outImage,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format   = format,
        .subresourceRange = {
            .aspectMask     = VK_IMAGE_ASPECT_DEPTH_BIT,
            .baseMipLevel   = 0,
            .levelCount     = 1,
            .baseArrayLayer = 0,
            .layerCount     = 1,
        },
    };
    vr = vkCreateImageView(ctx->logicalDevice, &ivci, NULL, outView);
    if (vr != VK_SUCCESS) {
        vkFreeMemory(ctx->logicalDevice, *outMemory, NULL);
        vkDestroyImage(ctx->logicalDevice, *outImage, NULL);
        return (VulkanResult){ .status = VULKAN_ERROR_IMAGE_VIEW_CREATION_FAILED, .vk_result = vr };
    }

    return (VulkanResult){ .status = VULKAN_SUCCESS, .vk_result = VK_SUCCESS };
}

static void destroy_depth_image(VkContext* ctx, DepthPrepassData* d) {
    if (d->depthView   != VK_NULL_HANDLE) vkDestroyImageView(ctx->logicalDevice, d->depthView, NULL);
    if (d->depthImage  != VK_NULL_HANDLE) vkDestroyImage(ctx->logicalDevice, d->depthImage, NULL);
    if (d->depthMemory != VK_NULL_HANDLE) vkFreeMemory(ctx->logicalDevice, d->depthMemory, NULL);
    d->depthView   = VK_NULL_HANDLE;
    d->depthImage  = VK_NULL_HANDLE;
    d->depthMemory = VK_NULL_HANDLE;
}

static VulkanResult depth_init(VkRenderer* renderer, RenderPass* pass, VkContext* ctx, VkWindow* window) {
    (void)renderer;
    DepthPrepassData* d   = (DepthPrepassData*)pass->pdata;
    VkDevice          dev = ctx->logicalDevice;

    d->depthFormat = VK_FORMAT_D32_SFLOAT;
    d->depthExtent = window->swapChainExtent;

    VulkanResult res = create_depth_image(ctx, d->depthExtent, d->depthFormat,
                                           &d->depthImage, &d->depthMemory, &d->depthView);
    if (res.status != VULKAN_SUCCESS) return res;

    VkDescriptorSetLayoutBinding uboBinding = {
        .binding         = 0,
        .descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .descriptorCount = 1,
        .stageFlags      = VK_SHADER_STAGE_VERTEX_BIT,
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
        res = vkBufferCreate(ctx, sizeof(GeometryPassUBO),
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

    return (VulkanResult){ .status = VULKAN_SUCCESS, .vk_result = VK_SUCCESS };
}

static void depth_destroy(RenderPass* pass, VkContext* ctx) {
    DepthPrepassData* d = (DepthPrepassData*)pass->pdata;

    pipelineCacheDestroy(&d->pipelines, ctx);
    destroy_depth_image(ctx, d);

    if (d->passPool      != VK_NULL_HANDLE) vkDestroyDescriptorPool(ctx->logicalDevice, d->passPool, NULL);
    if (d->passSetLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(ctx->logicalDevice, d->passSetLayout, NULL);
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        if (d->passUboBuffers[i].buffer) vkBufferDestroy(ctx, &d->passUboBuffers[i]);

    memset(d, 0, sizeof(DepthPrepassData));
}

static VulkanResult depth_on_resize(RenderPass* pass, VkContext* ctx, VkWindow* window) {
    DepthPrepassData* d = (DepthPrepassData*)pass->pdata;
    destroy_depth_image(ctx, d);
    d->depthExtent = window->swapChainExtent;
    return create_depth_image(ctx, d->depthExtent, d->depthFormat,
                               &d->depthImage, &d->depthMemory, &d->depthView);
}

static void depth_execute(RenderPass* pass, const RenderFrameContext* fc) {
    DepthPrepassData* d   = (DepthPrepassData*)pass->pdata;
    VkCommandBuffer   cmd = fc->cmd;
    uint32_t          fi  = fc->frameIndex;

    memcpy(d->passUboBuffers[fi].mappedData, &d->geometryData->ubo, sizeof(GeometryPassUBO));

    VkImageMemoryBarrier2 toDepthWrite = {
        .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask        = VK_PIPELINE_STAGE_2_NONE,
        .srcAccessMask       = VK_ACCESS_2_NONE,
        .dstStageMask        = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                               VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
        .dstAccessMask       = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                               VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        .oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout           = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        .image               = d->depthImage,
        .subresourceRange    = {
            .aspectMask     = VK_IMAGE_ASPECT_DEPTH_BIT,
            .baseMipLevel   = 0,
            .levelCount     = 1,
            .baseArrayLayer = 0,
            .layerCount     = 1,
        },
    };
    VkDependencyInfo dep = {
        .sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers    = &toDepthWrite,
    };
    vkCmdPipelineBarrier2(cmd, &dep);

    VkRenderingAttachmentInfo depthAttachment = {
        .sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView   = d->depthView,
        .imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        .loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp     = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue  = { .depthStencil = { 1.0f, 0 } },
    };
    VkRenderingInfo ri = {
        .sType                = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea           = { .offset = {0, 0}, .extent = d->depthExtent },
        .layerCount           = 1,
        .colorAttachmentCount = 0,
        .pColorAttachments    = NULL,
        .pDepthAttachment     = &depthAttachment,
    };
    vkCmdBeginRendering(cmd, &ri);

    VkViewport vp = {
        .x = 0, .y = 0,
        .width    = (float)d->depthExtent.width,
        .height   = (float)d->depthExtent.height,
        .minDepth = 0.0f, .maxDepth = 1.0f,
    };
    vkCmdSetViewport(cmd, 0, 1, &vp);
    VkRect2D scissor = { .offset = {0, 0}, .extent = d->depthExtent };
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    GeometryPassData* geo  = d->geometryData;
    CullPassData*     cull = d->cullData;

    PipelineCacheConfig cfg = {
        .passType        = PASS_TYPE_DEPTH,
        .globalSetLayout = fc->ctx->globalSetLayout,
        .passSetLayout   = d->passSetLayout,
        .colorFormat     = VK_FORMAT_UNDEFINED,
        .depthFormat     = d->depthFormat,
        .sampleCount     = VK_SAMPLE_COUNT_1_BIT,
    };

    for (uint32_t b = 0; b < geo->batchCount; b++) {
        GeometryBatch* batch = &geo->batches[b];
        RenderObject*  o     = batch->representativeObject;

        if (!o->mesh) continue;

        PipelineEntry* e = pipelineCacheGet(&d->pipelines, fc->ctx, &cfg, o->material);
        if (!e) continue;

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, e->pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, e->layout,
                                0, 1, &fc->ctx->globalDescriptorSets[fi], 0, NULL);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, e->layout,
                                1, 1, &d->passSets[fi], 0, NULL);

        vkMeshBind(o->mesh, cmd);
        vkCmdDrawIndexedIndirectCount(cmd,
            cull->drawCommandBuffers[fi].buffer,
            batch->drawCommandOffset * sizeof(VkDrawIndexedIndirectCommand),
            cull->drawCountBuffers[fi].buffer,
            batch->drawCountOffset,
            batch->maxDrawCount,
            sizeof(VkDrawIndexedIndirectCommand));
    }

    vkCmdEndRendering(cmd);

    VkImageMemoryBarrier2 toShaderRead = {
        .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask        = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                               VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
        .srcAccessMask       = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        .dstStageMask        = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        .dstAccessMask       = VK_ACCESS_2_SHADER_READ_BIT,
        .oldLayout           = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        .newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .image               = d->depthImage,
        .subresourceRange    = {
            .aspectMask     = VK_IMAGE_ASPECT_DEPTH_BIT,
            .baseMipLevel   = 0,
            .levelCount     = 1,
            .baseArrayLayer = 0,
            .layerCount     = 1,
        },
    };
    VkDependencyInfo postDep = {
        .sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers    = &toShaderRead,
    };
    vkCmdPipelineBarrier2(cmd, &postDep);
}

RenderPass depthPrepassCreate(DepthPrepassData* pdata) {
    return renderPassMake("depth_prepass", (RenderPassInterface){
        .init      = depth_init,
        .destroy   = depth_destroy,
        .execute   = depth_execute,
        .on_resize = depth_on_resize,
    }, pdata);
}