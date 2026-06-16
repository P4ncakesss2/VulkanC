#ifndef PIPELINE_CACHE_H
#define PIPELINE_CACHE_H

#include "vulkan_ctx.h"
#include "material.h"
#include "buffer.h"

#define PIPELINE_CACHE_MAX 64

typedef struct PipelineEntry {
    VkMaterial*           mat;
    VkPipeline            pipeline;
    VkPipelineLayout      layout;
    VkDescriptorSetLayout matSetLayout;
    VkDescriptorPool      matPool;
    VkDescriptorSet       matSets[MAX_FRAMES_IN_FLIGHT];
    VulkanBuffer          matUbo[MAX_FRAMES_IN_FLIGHT];
    bool                  hasMatSet;
} PipelineEntry;

typedef struct PipelineCache {
    PipelineEntry entries[PIPELINE_CACHE_MAX];
    uint32_t      count;
} PipelineCache;

typedef struct PipelineCacheConfig {
    VkPassType            passType;
    VkDescriptorSetLayout globalSetLayout;
    VkDescriptorSetLayout passSetLayout;
    VkFormat              colorFormat;
    VkFormat              depthFormat;
} PipelineCacheConfig;

PipelineEntry* pipelineCacheGet(PipelineCache* cache, VkContext* ctx,
                                 const PipelineCacheConfig* cfg, VkMaterial* mat);

void pipelineCacheDestroy(PipelineCache* cache, VkContext* ctx);

#endif