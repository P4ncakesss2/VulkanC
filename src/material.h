#ifndef VK_MATERIAL_H
#define VK_MATERIAL_H

#include "vulkan_ctx.h"
#include <stdbool.h>

typedef struct VkMaterial {
    VkPipeline       handle;
    VkPipelineLayout layout;
} VkMaterial;

typedef struct VkShaderStageCreateInfo {
    VkShaderStageFlagBits stage;
    const char* path;
    const char* entryName;
} VkShaderStageCreateInfo;

typedef struct VkPipelineBuilder {
    VkShaderStageCreateInfo* stages;
    uint32_t                   stageCount;
    VkDescriptorSetLayout* setLayouts;
    uint32_t                   setLayoutCount;
    VkPushConstantRange* pushConstants;
    uint32_t                   pushConstantCount;
    uint32_t                                 vertexBindingCount;
    VkVertexInputBindingDescription          vertexBindings[4];
    uint32_t                                 vertexAttributeCount;
    VkVertexInputAttributeDescription        vertexAttributes[16];
    VkPrimitiveTopology        topology;
    VkBool32                   primitiveRestartEnable;
    VkPolygonMode              polygonMode;
    VkCullModeFlags            cullMode;
    VkFrontFace                frontFace;
    float                      lineWidth;
    VkSampleCountFlagBits      msaaSamples;
    VkBool32                   sampleShadingEnable;
    float                      minSampleShading;
    VkBool32                   depthTestEnable;
    VkBool32                   depthWriteEnable;
    VkCompareOp                depthCompareOp;
    VkPipelineColorBlendAttachmentState colorBlendAttachment;
    VkFormat                   colorFormat;
    VkFormat                   depthFormat;
} VkPipelineBuilder;

VkPipelineBuilder vkPipelineBuilderCreateDefault(void);
VulkanResult vkMaterialBuild(VkContext* ctx, VkPipelineBuilder* builder, VkMaterial* outMaterial);
void vkMaterialDestroy(VkContext* ctx, VkMaterial* material);

#endif