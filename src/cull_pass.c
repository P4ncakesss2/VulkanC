#include "cull_pass.h"
#include "pipeline_cache.h"
#include "logger.h"
#include "renderer.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

static void extract_frustum_planes(const mat4 viewProj, vec4 planes[6]) {
    for (int i = 0; i < 4; i++) planes[0][i] = viewProj[i][3] + viewProj[i][0];
    for (int i = 0; i < 4; i++) planes[1][i] = viewProj[i][3] - viewProj[i][0];
    for (int i = 0; i < 4; i++) planes[2][i] = viewProj[i][3] + viewProj[i][1];
    for (int i = 0; i < 4; i++) planes[3][i] = viewProj[i][3] - viewProj[i][1];
    for (int i = 0; i < 4; i++) planes[4][i] = viewProj[i][3] + viewProj[i][2];
    for (int i = 0; i < 4; i++) planes[5][i] = viewProj[i][3] - viewProj[i][2];

    for (int i = 0; i < 6; i++) {
        float len = sqrtf(planes[i][0]*planes[i][0] +
                          planes[i][1]*planes[i][1] +
                          planes[i][2]*planes[i][2]);
        if (len > 0.0f) {
            planes[i][0] /= len; planes[i][1] /= len;
            planes[i][2] /= len; planes[i][3] /= len;
        }
    }
}

static char* read_spv(const char* path, size_t* outSize) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    char* buf = malloc((size_t)sz);
    if (!buf) { fclose(f); return NULL; }
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) { free(buf); fclose(f); return NULL; }
    fclose(f); *outSize = (size_t)sz; return buf;
}

static VulkanResult cull_init(VkRenderer* renderer, RenderPass* pass, VkContext* ctx, VkWindow* window) {
    (void)renderer; (void)window;
    CullPassData* d   = (CullPassData*)pass->pdata;
    VkDevice      dev = ctx->logicalDevice;

    VkDescriptorSetLayoutBinding bindings[] = {
        {
            .binding         = 0,
            .descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .descriptorCount = 1,
            .stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT,
        },
        {
            .binding         = 1,
            .descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 1,
            .stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT,
        },
        {
            .binding         = 2,
            .descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 1,
            .stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT,
        },
        {
            .binding         = 3,
            .descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 1,
            .stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT,
        },
    };
    VkDescriptorSetLayoutCreateInfo lci = {
        .sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 4,
        .pBindings    = bindings,
    };
    VkResult vr = vkCreateDescriptorSetLayout(dev, &lci, NULL, &d->setLayout);
    if (vr != VK_SUCCESS)
        return (VulkanResult){ .status = VULKAN_ERROR_DESCRIPTOR_SET_LAYOUT_CREATION_FAILED, .vk_result = vr };

    VkDescriptorPoolSize poolSizes[] = {
        { .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount = MAX_FRAMES_IN_FLIGHT },
        { .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = MAX_FRAMES_IN_FLIGHT * 3 },
    };
    VkDescriptorPoolCreateInfo pci = {
        .sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets       = MAX_FRAMES_IN_FLIGHT,
        .poolSizeCount = 2,
        .pPoolSizes    = poolSizes,
    };
    vr = vkCreateDescriptorPool(dev, &pci, NULL, &d->pool);
    if (vr != VK_SUCCESS)
        return (VulkanResult){ .status = VULKAN_ERROR_DESCRIPTOR_POOL_CREATION_FAILED, .vk_result = vr };

    VkDescriptorSetLayout layouts[MAX_FRAMES_IN_FLIGHT];
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) layouts[i] = d->setLayout;
    VkDescriptorSetAllocateInfo dsai = {
        .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool     = d->pool,
        .descriptorSetCount = MAX_FRAMES_IN_FLIGHT,
        .pSetLayouts        = layouts,
    };
    vr = vkAllocateDescriptorSets(dev, &dsai, d->sets);
    if (vr != VK_SUCCESS)
        return (VulkanResult){ .status = VULKAN_ERROR_DESCRIPTOR_SET_ALLOCATION_FAILED, .vk_result = vr };

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VulkanResult res;

        res = vkBufferCreate(ctx, sizeof(CullPassUBO),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
            &d->cullUboBuffers[i]);
        if (res.status != VULKAN_SUCCESS) return res;

        res = vkBufferCreate(ctx,
            sizeof(VkDrawIndexedIndirectCommand) * MAX_OBJECTS,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
            VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT,
            &d->drawCommandBuffers[i]);
        if (res.status != VULKAN_SUCCESS) return res;

        res = vkBufferCreate(ctx, sizeof(uint32_t),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
            VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT,
            &d->drawCountBuffers[i]);
        if (res.status != VULKAN_SUCCESS) return res;

        VkDescriptorBufferInfo uboInfo = {
            .buffer = d->cullUboBuffers[i].buffer,
            .offset = 0, .range = sizeof(CullPassUBO),
        };
        VkDescriptorBufferInfo ssboInfo = {
            .buffer = ctx->objectStorageBuffer.buffer,
            .offset = (VkDeviceSize)i * ctx->objectFrameStride,
            .range  = sizeof(ObjectSSBO) * MAX_OBJECTS,
        };
        VkDescriptorBufferInfo drawCmdInfo = {
            .buffer = d->drawCommandBuffers[i].buffer,
            .offset = 0, .range = sizeof(VkDrawIndexedIndirectCommand) * MAX_OBJECTS,
        };
        VkDescriptorBufferInfo drawCountInfo = {
            .buffer = d->drawCountBuffers[i].buffer,
            .offset = 0, .range = sizeof(uint32_t),
        };

        VkWriteDescriptorSet writes[] = {
            {
                .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet          = d->sets[i], .dstBinding = 0,
                .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .pBufferInfo     = &uboInfo,
            },
            {
                .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet          = d->sets[i], .dstBinding = 1,
                .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .pBufferInfo     = &ssboInfo,
            },
            {
                .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet          = d->sets[i], .dstBinding = 2,
                .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .pBufferInfo     = &drawCmdInfo,
            },
            {
                .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet          = d->sets[i], .dstBinding = 3,
                .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .pBufferInfo     = &drawCountInfo,
            },
        };
        vkUpdateDescriptorSets(dev, 4, writes, 0, NULL);
    }

    VkPipelineLayoutCreateInfo plci = {
        .sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts    = &d->setLayout,
    };
    vr = vkCreatePipelineLayout(dev, &plci, NULL, &d->pipelineLayout);
    if (vr != VK_SUCCESS)
        return (VulkanResult){ .status = VULKAN_ERROR_PIPELINE_LAYOUT_CREATION_FAILED, .vk_result = vr };

    size_t spvSize = 0;
    char*  spv     = read_spv("shaders/cull.spv", &spvSize);
    if (!spv)
        return (VulkanResult){ .status = VULKAN_ERROR_FILE_READ_FAILED };

    VkShaderModuleCreateInfo smci = {
        .sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = spvSize,
        .pCode    = (const uint32_t*)spv,
    };
    VkShaderModule mod = VK_NULL_HANDLE;
    vr = vkCreateShaderModule(dev, &smci, NULL, &mod);
    free(spv);
    if (vr != VK_SUCCESS)
        return (VulkanResult){ .status = VULKAN_ERROR_PIPELINE_CREATION_FAILED, .vk_result = vr };

    VkComputePipelineCreateInfo cpci = {
        .sType  = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .layout = d->pipelineLayout,
        .stage  = {
            .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage  = VK_SHADER_STAGE_COMPUTE_BIT,
            .module = mod,
            .pName  = "main",
        },
    };
    vr = vkCreateComputePipelines(dev, VK_NULL_HANDLE, 1, &cpci, NULL, &d->pipeline);
    vkDestroyShaderModule(dev, mod, NULL);
    if (vr != VK_SUCCESS)
        return (VulkanResult){ .status = VULKAN_ERROR_PIPELINE_CREATION_FAILED, .vk_result = vr };

    return (VulkanResult){ .status = VULKAN_SUCCESS, .vk_result = VK_SUCCESS };
}

static void cull_destroy(RenderPass* pass, VkContext* ctx) {
    CullPassData* d   = (CullPassData*)pass->pdata;
    VkDevice      dev = ctx->logicalDevice;

    if (d->pipeline       != VK_NULL_HANDLE) vkDestroyPipeline(dev, d->pipeline, NULL);
    if (d->pipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(dev, d->pipelineLayout, NULL);
    if (d->pool           != VK_NULL_HANDLE) vkDestroyDescriptorPool(dev, d->pool, NULL);
    if (d->setLayout      != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(dev, d->setLayout, NULL);

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        if (d->cullUboBuffers[i].buffer)    vkBufferDestroy(ctx, &d->cullUboBuffers[i]);
        if (d->drawCommandBuffers[i].buffer) vkBufferDestroy(ctx, &d->drawCommandBuffers[i]);
        if (d->drawCountBuffers[i].buffer)   vkBufferDestroy(ctx, &d->drawCountBuffers[i]);
    }
    memset(d, 0, sizeof(CullPassData));
}

static void cull_execute(RenderPass* pass, const RenderFrameContext* fc) {
    CullPassData* d   = (CullPassData*)pass->pdata;
    VkCommandBuffer cmd = fc->cmd;
    uint32_t        fi  = fc->frameIndex;
    uint32_t        objectCount = *d->objectCount;

    vkCmdFillBuffer(cmd, d->drawCountBuffers[fi].buffer, 0, sizeof(uint32_t), 0);

    VkBufferMemoryBarrier2 countClear = {
        .sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
        .srcStageMask        = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
        .srcAccessMask       = VK_ACCESS_2_TRANSFER_WRITE_BIT,
        .dstStageMask        = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        .dstAccessMask       = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT,
        .buffer              = d->drawCountBuffers[fi].buffer,
        .offset              = 0,
        .size                = sizeof(uint32_t),
    };
    VkDependencyInfo dep = {
        .sType                    = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .bufferMemoryBarrierCount = 1,
        .pBufferMemoryBarriers    = &countClear,
    };
    vkCmdPipelineBarrier2(cmd, &dep);

    mat4 viewProj;
    glm_mat4_mul(d->geometryData->ubo.proj, d->geometryData->ubo.view, viewProj);

    CullPassUBO ubo = { .objectCount = objectCount };
    extract_frustum_planes(viewProj, ubo.frustumPlanes);
    memcpy(d->cullUboBuffers[fi].mappedData, &ubo, sizeof(CullPassUBO));

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, d->pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, d->pipelineLayout,
                             0, 1, &d->sets[fi], 0, NULL);
    vkCmdDispatch(cmd, (objectCount + 63) / 64, 1, 1);

    VkBufferMemoryBarrier2 postCompute[2] = {
        {
            .sType         = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
            .srcStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            .srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT,
            .dstStageMask  = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT,
            .dstAccessMask = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT,
            .buffer        = d->drawCommandBuffers[fi].buffer,
            .offset        = 0,
            .size          = VK_WHOLE_SIZE,
        },
        {
            .sType         = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
            .srcStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            .srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT,
            .dstStageMask  = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT,
            .dstAccessMask = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT,
            .buffer        = d->drawCountBuffers[fi].buffer,
            .offset        = 0,
            .size          = VK_WHOLE_SIZE,
        },
    };
    VkDependencyInfo postDep = {
        .sType                    = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .bufferMemoryBarrierCount = 2,
        .pBufferMemoryBarriers    = postCompute,
    };
    vkCmdPipelineBarrier2(cmd, &postDep);
}

RenderPass cullPassCreate(CullPassData* pdata) {
    return renderPassMake("cull", (RenderPassInterface){
        .init      = cull_init,
        .destroy   = cull_destroy,
        .execute   = cull_execute,
        .on_resize = NULL,
    }, pdata);
}