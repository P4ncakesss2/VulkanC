#ifndef CULL_PASS_H
#define CULL_PASS_H

#include "render_pass.h"
#include "render_types.h"
#include "buffer.h"
#include "geometry_pass.h"
#include <cglm/cglm.h>

typedef struct CullPassUBO {
    vec4 frustumPlanes[6];
    uint32_t objectCount;
    uint32_t pad[3];
} CullPassUBO;

typedef struct CullPassData {
    RenderObject*     objects;
    uint32_t*         objectCount;
    GeometryPassData* geometryData;

    VulkanBuffer cullUboBuffers[MAX_FRAMES_IN_FLIGHT];
    VulkanBuffer drawCommandBuffers[MAX_FRAMES_IN_FLIGHT];
    VulkanBuffer drawCountBuffers[MAX_FRAMES_IN_FLIGHT];

    VkDescriptorSetLayout setLayout;
    VkDescriptorPool      pool;
    VkDescriptorSet       sets[MAX_FRAMES_IN_FLIGHT];

    VkPipelineLayout pipelineLayout;
    VkPipeline       pipeline;
} CullPassData;

RenderPass cullPassCreate(CullPassData* pdata);

#endif