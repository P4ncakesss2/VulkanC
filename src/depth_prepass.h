#ifndef DEPTH_PREPASS_H
#define DEPTH_PREPASS_H

#include "render_pass.h"
#include "render_types.h"
#include "pipeline_cache.h"
#include "buffer.h"
#include "geometry_pass.h"
#include "cull_pass.h"
#include <cglm/cglm.h>

typedef struct DepthPrepassData {
    GeometryPassData* geometryData;
    CullPassData*     cullData;

    VkImage           depthImage;
    VkDeviceMemory    depthMemory;
    VkImageView       depthView;
    VkFormat          depthFormat;
    VkExtent2D        depthExtent;

    VkDescriptorSetLayout passSetLayout;
    VkDescriptorPool      passPool;
    VkDescriptorSet       passSets[MAX_FRAMES_IN_FLIGHT];
    VulkanBuffer          passUboBuffers[MAX_FRAMES_IN_FLIGHT];

    PipelineCache pipelines;
} DepthPrepassData;

RenderPass depthPrepassCreate(DepthPrepassData* pdata);

#endif