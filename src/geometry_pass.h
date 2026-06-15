#ifndef GEOMETRY_PASS_H
#define GEOMETRY_PASS_H

#include "render_pass.h"
#include "render_types.h"
#include "material.h"
#include "buffer.h"
#include <cglm/cglm.h>

typedef struct GeometryPassUBO {
    mat4 proj;
    mat4 view;
    mat4 invProj;
} GeometryPassUBO;

typedef struct GeometryPassData {
    RenderObject* objects;
    uint32_t      objectCount;
    GeometryPassUBO ubo;

    VkDescriptorSetLayout setLayout;
    VkDescriptorPool      pool;
    VkDescriptorSet       sets[MAX_FRAMES_IN_FLIGHT];
    VulkanBuffer          uboBuffers[MAX_FRAMES_IN_FLIGHT];

    VkFormat colorFormat;
    VkFormat depthFormat;
} GeometryPassData;

RenderPass geometryPassCreate(GeometryPassData* pdata);

#endif