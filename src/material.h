#ifndef VK_MATERIAL_H
#define VK_MATERIAL_H

#include "vulkan_ctx.h"
#include "buffer.h"
#include "renderer.h"
#include "spirv_reflect.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define MATERIAL_MAX_BINDINGS  16
#define MATERIAL_MAX_PARAMS    64
#define MATERIAL_NAME_MAX      64

typedef enum MaterialParamType {
    MATERIAL_PARAM_FLOAT,
    MATERIAL_PARAM_FLOAT2,
    MATERIAL_PARAM_FLOAT3,
    MATERIAL_PARAM_FLOAT4,
    MATERIAL_PARAM_INT,
    MATERIAL_PARAM_UINT,
    MATERIAL_PARAM_MAT4,
} MaterialParamType;

typedef struct MaterialParamValue {
    MaterialParamType type;
    union {
        float     f;
        float     f2[2];
        float     f3[3];
        float     f4[4];
        int32_t   i;
        uint32_t  u;
        float     mat4[16];
    };
} MaterialParamValue;

static inline MaterialParamValue mpFloat (float v)                             { return (MaterialParamValue){.type=MATERIAL_PARAM_FLOAT,  .f=v}; }
static inline MaterialParamValue mpFloat2(float x, float y)                   { return (MaterialParamValue){.type=MATERIAL_PARAM_FLOAT2, .f2={x,y}}; }
static inline MaterialParamValue mpFloat3(float x, float y, float z)          { return (MaterialParamValue){.type=MATERIAL_PARAM_FLOAT3, .f3={x,y,z}}; }
static inline MaterialParamValue mpFloat4(float x, float y, float z, float w) { return (MaterialParamValue){.type=MATERIAL_PARAM_FLOAT4, .f4={x,y,z,w}}; }
static inline MaterialParamValue mpInt   (int32_t v)                          { return (MaterialParamValue){.type=MATERIAL_PARAM_INT,    .i=v}; }
static inline MaterialParamValue mpUint  (uint32_t v)                         { return (MaterialParamValue){.type=MATERIAL_PARAM_UINT,   .u=v}; }

typedef struct MaterialFieldDesc {
    const char* name;
    uint32_t          offset;
    MaterialParamType type;
} MaterialFieldDesc;

#define MATERIAL_FIELD(T, member, paramType) \
    { #member, (uint32_t)offsetof(T, member), paramType }

typedef struct MaterialParamEntry {
    char              name[MATERIAL_NAME_MAX];
    MaterialParamType type;
    uint32_t          binding;
    uint32_t          offset;
    uint32_t          size;
} MaterialParamEntry;

typedef struct MaterialUBOBuffer {
    uint32_t     binding;
    uint8_t* data;
    uint32_t     totalSize;
    VulkanBuffer gpuBuffers[MAX_FRAMES_IN_FLIGHT];
} MaterialUBOBuffer;

typedef struct VkMaterial {
    VkPipeline       pipelines[PASS_TYPE_COUNT];
    VkPipelineLayout pipelineLayouts[PASS_TYPE_COUNT];
    bool             isCompute; 

    VkDescriptorSetLayout setLayout;
    VkDescriptorPool      pool;
    VkDescriptorSet       sets[MAX_FRAMES_IN_FLIGHT];
    bool                  hasSet;

    MaterialParamEntry params[MATERIAL_MAX_PARAMS];
    uint32_t           paramCount;

    MaterialUBOBuffer  uboBuffers[MATERIAL_MAX_BINDINGS];
    uint32_t           uboCount;

    bool               needsFlush[MAX_FRAMES_IN_FLIGHT];
} VkMaterial;

typedef struct VkShaderStageCreateInfo {
    VkShaderStageFlagBits stage;
    VkPassType              pass;
    const char* path;
    const char* entryName;
} VkShaderStageCreateInfo;

typedef struct VkPipelineBuilder {
    VkShaderStageCreateInfo* stages;
    uint32_t                 stageCount;

    uint32_t                          vertexBindingCount;
    VkVertexInputBindingDescription   vertexBindings[4];
    uint32_t                          vertexAttributeCount;
    VkVertexInputAttributeDescription vertexAttributes[16];

    VkPrimitiveTopology   topology;
    VkBool32              primitiveRestartEnable;
    VkPolygonMode         polygonMode;
    VkCullModeFlags       cullMode;
    VkFrontFace           frontFace;
    float                 lineWidth;
    VkSampleCountFlagBits msaaSamples;
    VkBool32              sampleShadingEnable;
    float                 minSampleShading;
    VkBool32              depthTestEnable;
    VkBool32              depthWriteEnable;
    VkCompareOp           depthCompareOp;

    VkPipelineColorBlendAttachmentState colorBlendAttachment;
    VkFormat colorFormat;
    VkFormat depthFormat;
} VkPipelineBuilder;

VkPipelineBuilder vkPipelineBuilderCreateDefault(void);

VulkanResult vkMaterialBuildDescriptorSet(VkContext* ctx,
                                           VkPipelineBuilder* builder,
                                           VkMaterial* mat);

VulkanResult vkMaterialBuildForPass(VkContext* ctx,
                                     VkRenderer* renderer,
                                     VkPipelineBuilder* builder,
                                     VkPassType passType,
                                     VkMaterial* mat);

VulkanResult vkMaterialBuild(VkContext* ctx,
                              VkRenderer* renderer,
                              VkPipelineBuilder* builder,
                              VkMaterial* out);

void vkMaterialDestroy(VkContext* ctx, VkMaterial* material);

void vkMaterialBindPipeline(VkCommandBuffer cmd, VkMaterial* material, VkPassType passType);

VkPipelineBindPoint vkMaterialGetBindPoint(VkMaterial* material);

bool vkMaterialSetParam(VkMaterial* mat, const char* name, MaterialParamValue value);
void vkMaterialSetParams(VkMaterial* mat, const void* srcStruct,
                         const MaterialFieldDesc* fields, uint32_t fieldCount);
void vkMaterialFlush(VkMaterial* mat, VkDevice device, uint32_t frameIndex);

static inline bool vkMaterialSupportsPass(const VkMaterial* mat, VkPassType passType) {
    return mat->pipelines[passType] != VK_NULL_HANDLE;
}

#endif