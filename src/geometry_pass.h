#ifndef GEOMETRY_PASS_H
#define GEOMETRY_PASS_H

#include "render_pass.h"
#include "render_types.h"
#include "pipeline_cache.h"
#include "buffer.h"
#include <cglm/cglm.h>

typedef struct CullPassData CullPassData;

typedef struct GeometryPassUBO {
    mat4 proj;
    mat4 view;
    mat4 invProj;
    
    vec4 cameraPos;
    uint32_t irradianceMapIndex;
    uint32_t radianceMapIndex;
    uint32_t brdfLutIndex;
    uint32_t radianceMipCount;
} GeometryPassUBO;

typedef struct GeometryBatch {
    RenderObject* representativeObject;
    uint32_t      drawCommandOffset;
    uint32_t      drawCountOffset;
    uint32_t      maxDrawCount;
} GeometryBatch;

typedef struct GeometryPassData {
    RenderObject*   objects;
    uint32_t        objectCount;
    GeometryBatch   batches[MAX_OBJECTS];
    uint32_t        batchCount;
    GeometryPassUBO ubo;
    CullPassData*   cullData;
    VkDescriptorSetLayout passSetLayout;
    VkDescriptorPool      passPool;
    VkDescriptorSet       passSets[MAX_FRAMES_IN_FLIGHT];
    VulkanBuffer          passUboBuffers[MAX_FRAMES_IN_FLIGHT];
    PipelineCache   pipelines;
    VkFormat        colorFormat;
    VkFormat        depthFormat;
} GeometryPassData;

RenderPass geometryPassCreate(GeometryPassData* pdata);

#endif