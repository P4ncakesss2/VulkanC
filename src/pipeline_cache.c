#include "pipeline_cache.h"
#include "logger.h"
#include "mesh.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static char* read_spv(const char* path, size_t* outSize) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    char* buf = malloc((size_t)sz);
    if (!buf) { fclose(f); return NULL; }
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) { free(buf); fclose(f); return NULL; }
    fclose(f); *outSize = (size_t)sz; return buf;
}

static VkShaderModule make_module(VkDevice dev, const char* code, size_t size) {
    VkShaderModuleCreateInfo ci = {
        .sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = size,
        .pCode    = (const uint32_t*)code,
    };
    VkShaderModule m = VK_NULL_HANDLE;
    vkCreateShaderModule(dev, &ci, NULL, &m);
    return m;
}

static VulkanResult build_entry(VkContext* ctx, const PipelineCacheConfig* cfg,
                                 VkMaterial* mat, PipelineEntry* e)
{
    memset(e, 0, sizeof(*e));
    e->mat = mat;

    const VkPipelineBuilder* b = &mat->builder;

    if (mat->uboParams.size > 0) {
        VkDescriptorSetLayoutBinding binding = {
            .binding         = mat->uboParams.binding,
            .descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .descriptorCount = 1,
            .stageFlags      = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        };
        VkDescriptorSetLayoutCreateInfo lci = {
            .sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .bindingCount = 1,
            .pBindings    = &binding,
        };
        VkResult r = vkCreateDescriptorSetLayout(ctx->logicalDevice, &lci, NULL, &e->matSetLayout);
        if (r != VK_SUCCESS)
            return (VulkanResult){.status = VULKAN_ERROR_DESCRIPTOR_SET_LAYOUT_CREATION_FAILED, .vk_result = r};

        VkDescriptorPoolSize ps = {
            .type            = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .descriptorCount = MAX_FRAMES_IN_FLIGHT,
        };
        VkDescriptorPoolCreateInfo pci = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .maxSets = MAX_FRAMES_IN_FLIGHT, .poolSizeCount = 1, .pPoolSizes = &ps,
        };
        r = vkCreateDescriptorPool(ctx->logicalDevice, &pci, NULL, &e->matPool);
        if (r != VK_SUCCESS) {
            vkDestroyDescriptorSetLayout(ctx->logicalDevice, e->matSetLayout, NULL);
            return (VulkanResult){.status = VULKAN_ERROR_DESCRIPTOR_POOL_CREATION_FAILED, .vk_result = r};
        }

        VkDescriptorSetLayout layouts[MAX_FRAMES_IN_FLIGHT];
        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) layouts[i] = e->matSetLayout;
        VkDescriptorSetAllocateInfo dsai = {
            .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool     = e->matPool,
            .descriptorSetCount = MAX_FRAMES_IN_FLIGHT,
            .pSetLayouts        = layouts,
        };
        r = vkAllocateDescriptorSets(ctx->logicalDevice, &dsai, e->matSets);
        if (r != VK_SUCCESS) {
            vkDestroyDescriptorPool(ctx->logicalDevice, e->matPool, NULL);
            vkDestroyDescriptorSetLayout(ctx->logicalDevice, e->matSetLayout, NULL);
            return (VulkanResult){.status = VULKAN_ERROR_DESCRIPTOR_SET_ALLOCATION_FAILED, .vk_result = r};
        }

        for (uint32_t f = 0; f < MAX_FRAMES_IN_FLIGHT; f++) {
            VulkanResult vr = vkBufferCreate(ctx, mat->uboParams.size,
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
                &e->matUbo[f]);
            if (vr.status != VULKAN_SUCCESS) return vr;

            VkDescriptorBufferInfo bi = {
                .buffer = e->matUbo[f].buffer, .offset = 0, .range = mat->uboParams.size,
            };
            VkWriteDescriptorSet w = {
                .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet          = e->matSets[f],
                .dstBinding      = mat->uboParams.binding,
                .descriptorCount = 1,
                .descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .pBufferInfo     = &bi,
            };
            vkUpdateDescriptorSets(ctx->logicalDevice, 1, &w, 0, NULL);
        }
        e->hasMatSet = true;
    }

    VkDescriptorSetLayout setLayouts[3] = { cfg->globalSetLayout, cfg->passSetLayout };
    uint32_t setLayoutCount = 2;
    if (e->hasMatSet) setLayouts[setLayoutCount++] = e->matSetLayout;

    VkPipelineLayoutCreateInfo plci = {
        .sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = setLayoutCount,
        .pSetLayouts    = setLayouts,
    };
    VkResult r = vkCreatePipelineLayout(ctx->logicalDevice, &plci, NULL, &e->layout);
    if (r != VK_SUCCESS)
        return (VulkanResult){.status = VULKAN_ERROR_PIPELINE_LAYOUT_CREATION_FAILED, .vk_result = r};

    uint32_t activeCount = 0;
    for (uint32_t i = 0; i < b->stageCount; i++)
        if (b->stages[i].pass == cfg->passType) activeCount++;

    VkPipelineShaderStageCreateInfo* stages  = malloc(sizeof(*stages)  * activeCount);
    VkShaderModule*                  modules = malloc(sizeof(*modules) * activeCount);
    uint32_t idx = 0;

    for (uint32_t i = 0; i < b->stageCount; i++) {
        if (b->stages[i].pass != cfg->passType) continue;
        size_t sz = 0;
        char* code = read_spv(b->stages[i].path, &sz);
        if (!code) {
            for (uint32_t j = 0; j < idx; j++) vkDestroyShaderModule(ctx->logicalDevice, modules[j], NULL);
            free(stages); free(modules);
            vkDestroyPipelineLayout(ctx->logicalDevice, e->layout, NULL);
            return (VulkanResult){.status = VULKAN_ERROR_FILE_READ_FAILED};
        }
        modules[idx] = make_module(ctx->logicalDevice, code, sz);
        free(code);
        stages[idx] = (VkPipelineShaderStageCreateInfo){
            .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage  = b->stages[i].stage,
            .module = modules[idx],
            .pName  = b->stages[i].entryName,
        };
        idx++;
    }

    VkFormat colorFmt = cfg->colorFormat != VK_FORMAT_UNDEFINED ? cfg->colorFormat : b->colorFormat;
    VkFormat depthFmt = cfg->depthFormat != VK_FORMAT_UNDEFINED ? cfg->depthFormat : b->depthFormat;

    VkPipelineVertexInputStateCreateInfo vi = {
        .sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount   = b->vertexBindingCount,
        .pVertexBindingDescriptions      = b->vertexBindingCount  > 0 ? b->vertexBindings   : NULL,
        .vertexAttributeDescriptionCount = b->vertexAttributeCount,
        .pVertexAttributeDescriptions    = b->vertexAttributeCount > 0 ? b->vertexAttributes : NULL,
    };
    VkPipelineInputAssemblyStateCreateInfo ia = {
        .sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = b->topology,
    };
    VkDynamicState dynStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dyn = {
        .sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = 2, .pDynamicStates = dynStates,
    };
    VkPipelineViewportStateCreateInfo vp = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1, .scissorCount = 1,
    };
    VkPipelineRasterizationStateCreateInfo rast = {
        .sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = b->polygonMode, .cullMode  = b->cullMode,
        .frontFace   = b->frontFace,   .lineWidth = b->lineWidth,
    };
    VkPipelineMultisampleStateCreateInfo ms = {
        .sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = b->msaaSamples,
        .sampleShadingEnable  = b->sampleShadingEnable,
        .minSampleShading     = b->minSampleShading,
    };
    VkPipelineColorBlendStateCreateInfo cb = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1, .pAttachments = &b->colorBlendAttachment,
    };
    VkPipelineDepthStencilStateCreateInfo ds = {
        .sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable  = b->depthTestEnable,
        .depthWriteEnable = b->depthWriteEnable,
        .depthCompareOp   = b->depthCompareOp,
    };
    VkPipelineRenderingCreateInfo rci = {
        .sType                   = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount    = 1,
        .pColorAttachmentFormats = &colorFmt,
        .depthAttachmentFormat   = depthFmt,
    };
    VkGraphicsPipelineCreateInfo gci = {
        .sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext               = &rci,
        .stageCount          = activeCount,
        .pStages             = stages,
        .pVertexInputState   = &vi,
        .pInputAssemblyState = &ia,
        .pDepthStencilState  = &ds,
        .pViewportState      = &vp,
        .pRasterizationState = &rast,
        .pMultisampleState   = &ms,
        .pColorBlendState    = &cb,
        .pDynamicState       = &dyn,
        .layout              = e->layout,
    };
    r = vkCreateGraphicsPipelines(ctx->logicalDevice, VK_NULL_HANDLE, 1, &gci, NULL, &e->pipeline);
    for (uint32_t i = 0; i < activeCount; i++) vkDestroyShaderModule(ctx->logicalDevice, modules[i], NULL);
    free(stages); free(modules);

    if (r != VK_SUCCESS) {
        vkDestroyPipelineLayout(ctx->logicalDevice, e->layout, NULL);
        return (VulkanResult){.status = VULKAN_ERROR_PIPELINE_CREATION_FAILED, .vk_result = r};
    }
    return (VulkanResult){.status = VULKAN_SUCCESS, .vk_result = VK_SUCCESS};
}

PipelineEntry* pipelineCacheGet(PipelineCache* cache, VkContext* ctx,
                                 const PipelineCacheConfig* cfg, VkMaterial* mat)
{
    for (uint32_t i = 0; i < cache->count; i++)
        if (cache->entries[i].mat == mat) return &cache->entries[i];

    if (cache->count >= PIPELINE_CACHE_MAX) {
        LOG_ERROR("pipelineCacheGet: cache full");
        return NULL;
    }
    PipelineEntry* e = &cache->entries[cache->count];
    VulkanResult r = build_entry(ctx, cfg, mat, e);
    if (r.status != VULKAN_SUCCESS) {
        LOG_ERROR("pipelineCacheGet: build failed (%d)", r.status);
        return NULL;
    }
    cache->count++;
    return e;
}

void pipelineCacheDestroy(PipelineCache* cache, VkContext* ctx) {
    VkDevice dev = ctx->logicalDevice;
    for (uint32_t i = 0; i < cache->count; i++) {
        PipelineEntry* e = &cache->entries[i];
        if (e->pipeline != VK_NULL_HANDLE) vkDestroyPipeline(dev, e->pipeline, NULL);
        if (e->layout   != VK_NULL_HANDLE) vkDestroyPipelineLayout(dev, e->layout, NULL);
        if (e->hasMatSet) {
            for (uint32_t f = 0; f < MAX_FRAMES_IN_FLIGHT; f++)
                vkBufferDestroy(ctx, &e->matUbo[f]);
            if (e->matPool      != VK_NULL_HANDLE) vkDestroyDescriptorPool(dev, e->matPool, NULL);
            if (e->matSetLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(dev, e->matSetLayout, NULL);
        }
    }
    memset(cache, 0, sizeof(*cache));
}