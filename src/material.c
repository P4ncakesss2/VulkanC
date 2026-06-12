#include "material.h"
#include "mesh.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

static char* read_file(const char* filename, size_t* outSize) {
    FILE* file = fopen(filename, "rb");
    if (!file) {
        LOG_ERROR("Failed to open file: %s", filename);
        return NULL;
    }

    fseek(file, 0, SEEK_END);
    long fileSize = ftell(file);
    if (fileSize < 0) {
        LOG_ERROR("Failed to determine size of file: %s", filename);
        fclose(file);
        return NULL;
    }
    
    fseek(file, 0, SEEK_SET);

    char* buffer = (char*)malloc(fileSize);
    if (!buffer) {
        LOG_ERROR("Failed to allocate memory for file: %s", filename);
        fclose(file);
        return NULL;
    }

    size_t bytesRead = fread(buffer, 1, fileSize, file);
    if (bytesRead != (size_t)fileSize) {
        LOG_ERROR("Failed to read entire file: %s", filename);
        free(buffer);
        fclose(file);
        return NULL;
    }

    fclose(file);
    *outSize = (size_t)fileSize;
    return buffer;
}

static VkShaderModule create_shader_module(VkDevice device, const char* code, size_t code_size) {
    VkShaderModuleCreateInfo createInfo = {
        .sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = code_size,
        .pCode    = (const uint32_t*)code
    };

    VkShaderModule shaderModule;
    if (vkCreateShaderModule(device, &createInfo, NULL, &shaderModule) != VK_SUCCESS) {
        LOG_ERROR("Failed to create shader module!");
        return VK_NULL_HANDLE;
    }
    return shaderModule;
}

VkPipelineBuilder vkPipelineBuilderCreateDefault(void) {
    VkPipelineBuilder builder = {0};

    builder.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    builder.primitiveRestartEnable = VK_FALSE;
    builder.polygonMode = VK_POLYGON_MODE_FILL;
    builder.cullMode = VK_CULL_MODE_BACK_BIT;
    builder.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    builder.lineWidth = 1.0f;
    builder.msaaSamples = VK_SAMPLE_COUNT_1_BIT;
    builder.sampleShadingEnable = VK_FALSE;
    builder.minSampleShading = 1.0f;
    builder.depthTestEnable = VK_TRUE;
    builder.depthWriteEnable = VK_TRUE;
    builder.depthCompareOp = VK_COMPARE_OP_LESS;

    builder.colorBlendAttachment.blendEnable = VK_FALSE;
    builder.colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | 
                                                  VK_COLOR_COMPONENT_G_BIT | 
                                                  VK_COLOR_COMPONENT_B_BIT | 
                                                  VK_COLOR_COMPONENT_A_BIT;

    builder.vertexBindingCount = 1;
    builder.vertexBindings[0] = vkVertexGetBindingDescription();
    builder.vertexAttributeCount = 4;
    vkVertexGetAttributeDescription(builder.vertexAttributes);

    return builder;
}

VulkanResult vkMaterialBuild(VkContext* ctx, VkPipelineBuilder* builder, VkMaterial* outMaterial) {
    if (builder->stageCount == 0) {
        return (VulkanResult){.status = VULKAN_ERROR_PIPELINE_CREATION_FAILED, .vk_result = VK_ERROR_INITIALIZATION_FAILED};
    }

    VkPipelineShaderStageCreateInfo* shaderStages = malloc(sizeof(VkPipelineShaderStageCreateInfo) * builder->stageCount);
    VkShaderModule* shaderModules = malloc(sizeof(VkShaderModule) * builder->stageCount);

    for (uint32_t i = 0; i < builder->stageCount; i++) {
        size_t shaderSize = 0;
        char* shaderCode = read_file(builder->stages[i].path, &shaderSize);
        if (!shaderCode) {
            for (uint32_t j = 0; j < i; j++) vkDestroyShaderModule(ctx->logicalDevice, shaderModules[j], NULL);
            free(shaderStages); free(shaderModules);
            return (VulkanResult){.status = VULKAN_ERROR_FILE_READ_FAILED, .vk_result = VK_ERROR_UNKNOWN};
        }

        shaderModules[i] = create_shader_module(ctx->logicalDevice, shaderCode, shaderSize);
        free(shaderCode);

        shaderStages[i] = (VkPipelineShaderStageCreateInfo){
            .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage  = builder->stages[i].stage,
            .module = shaderModules[i],
            .pName  = builder->stages[i].entryName,
        };
    }

    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {
        .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount         = builder->setLayoutCount,
        .pSetLayouts            = builder->setLayouts,
        .pushConstantRangeCount = builder->pushConstantCount,
        .pPushConstantRanges    = builder->pushConstants,
    };

    VkResult layoutResult = vkCreatePipelineLayout(ctx->logicalDevice, &pipelineLayoutInfo, NULL, &outMaterial->layout);
    if (layoutResult != VK_SUCCESS) {
        for (uint32_t i = 0; i < builder->stageCount; i++) vkDestroyShaderModule(ctx->logicalDevice, shaderModules[i], NULL);
        free(shaderStages); free(shaderModules);
        return (VulkanResult){.status = VULKAN_ERROR_PIPELINE_LAYOUT_CREATION_FAILED, .vk_result = layoutResult};
    }

    VkResult pipelineResult;
    bool isCompute = (builder->stages[0].stage == VK_SHADER_STAGE_COMPUTE_BIT);

    if (isCompute) {
        VkComputePipelineCreateInfo computeInfo = {
            .sType  = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
            .layout = outMaterial->layout,
            .stage  = shaderStages[0]
        };
        pipelineResult = vkCreateComputePipelines(ctx->logicalDevice, VK_NULL_HANDLE, 1, &computeInfo, NULL, &outMaterial->handle);
    } 
    else {
        VkPipelineVertexInputStateCreateInfo vertexInputInfo = {
            .sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
            .vertexBindingDescriptionCount   = builder->vertexBindingCount,
            .pVertexBindingDescriptions      = builder->vertexBindingCount > 0 ? builder->vertexBindings : NULL,
            .vertexAttributeDescriptionCount = builder->vertexAttributeCount,
            .pVertexAttributeDescriptions    = builder->vertexAttributeCount > 0 ? builder->vertexAttributes : NULL,
        };

        VkPipelineInputAssemblyStateCreateInfo inputAssembly = {
            .sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
            .topology               = builder->topology,
            .primitiveRestartEnable = builder->primitiveRestartEnable
        };

        VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo dynamicStateCreateInfo = {
            .sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
            .dynamicStateCount = 2,
            .pDynamicStates    = dynamicStates
        };

        VkPipelineViewportStateCreateInfo viewportState = {
            .sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
            .viewportCount = 1,
            .scissorCount  = 1,
        };

        VkPipelineRasterizationStateCreateInfo rasterizer = {
            .sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
            .polygonMode = builder->polygonMode,
            .cullMode    = builder->cullMode,
            .frontFace   = builder->frontFace,
            .lineWidth   = builder->lineWidth
        };

        VkPipelineMultisampleStateCreateInfo multisampling = {
            .sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
            .rasterizationSamples = builder->msaaSamples,
            .sampleShadingEnable  = builder->sampleShadingEnable,
            .minSampleShading     = builder->minSampleShading,
        };

        VkPipelineColorBlendStateCreateInfo colorBlending = {
            .sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
            .attachmentCount = 1,
            .pAttachments    = &builder->colorBlendAttachment,
        };

        VkPipelineDepthStencilStateCreateInfo depthStencil = {
            .sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
            .depthTestEnable  = builder->depthTestEnable,
            .depthWriteEnable = builder->depthWriteEnable,
            .depthCompareOp   = builder->depthCompareOp,
        };

        VkPipelineRenderingCreateInfo pipelineRenderingCreateInfo = {
            .sType                   = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
            .colorAttachmentCount    = 1,
            .pColorAttachmentFormats = &builder->colorFormat,
            .depthAttachmentFormat   = builder->depthFormat,
        };

        VkGraphicsPipelineCreateInfo graphicsInfo = {
            .sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
            .pNext               = &pipelineRenderingCreateInfo,
            .stageCount          = builder->stageCount,
            .pStages             = shaderStages,
            .pVertexInputState   = &vertexInputInfo,
            .pInputAssemblyState = &inputAssembly,
            .pDepthStencilState  = &depthStencil,
            .pViewportState      = &viewportState,
            .pRasterizationState = &rasterizer,
            .pMultisampleState   = &multisampling,
            .pColorBlendState    = &colorBlending,
            .pDynamicState       = &dynamicStateCreateInfo,
            .layout              = outMaterial->layout,
        };

        pipelineResult = vkCreateGraphicsPipelines(ctx->logicalDevice, VK_NULL_HANDLE, 1, &graphicsInfo, NULL, &outMaterial->handle);
    }

    for (uint32_t i = 0; i < builder->stageCount; i++) {
        vkDestroyShaderModule(ctx->logicalDevice, shaderModules[i], NULL);
    }
    free(shaderStages);
    free(shaderModules);

    if (pipelineResult != VK_SUCCESS) {
        return (VulkanResult){.status = VULKAN_ERROR_PIPELINE_CREATION_FAILED, .vk_result = pipelineResult};
    }

    return (VulkanResult){.status = VULKAN_SUCCESS, .vk_result = VK_SUCCESS};
}

void vkMaterialDestroy(VkContext* ctx, VkMaterial* material) {
    if (material->handle != VK_NULL_HANDLE) {
        vkDestroyPipeline(ctx->logicalDevice, material->handle, NULL);
        material->handle = VK_NULL_HANDLE;
    }
    if (material->layout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(ctx->logicalDevice, material->layout, NULL);
        material->layout = VK_NULL_HANDLE;
    }
}