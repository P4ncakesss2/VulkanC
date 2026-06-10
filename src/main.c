#include "vulkan_ctx.h"
#include "window.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "render_types.h"
#include "logger.h"

typedef struct Vertex {
    vec3 position;
    vec3 color;
} Vertex;

static Vertex boxVertices[] = {
    {{-0.5f, -0.5f,  0.5f}, {1.0f, 0.0f, 0.0f}},
    {{ 0.5f, -0.5f,  0.5f}, {0.0f, 1.0f, 0.0f}},
    {{ 0.5f,  0.5f,  0.5f}, {0.0f, 0.0f, 1.0f}},
    {{-0.5f,  0.5f,  0.5f}, {1.0f, 1.0f, 0.0f}},
    {{-0.5f, -0.5f, -0.5f}, {1.0f, 0.0f, 1.0f}},
    {{ 0.5f, -0.5f, -0.5f}, {0.0f, 1.0f, 1.0f}},
    {{ 0.5f,  0.5f, -0.5f}, {1.0f, 1.0f, 1.0f}},
    {{-0.5f,  0.5f, -0.5f}, {0.5f, 0.5f, 0.5f}},
};

static uint32_t boxIndices[] = {
    0, 1, 2,  2, 3, 0,  // front
    5, 4, 7,  7, 6, 5,  // back
    4, 0, 3,  3, 7, 4,  // left
    1, 5, 6,  6, 2, 1,  // right
    3, 2, 6,  6, 7, 3,  // top
    4, 5, 1,  1, 0, 4,  // bottom
};

typedef struct GeoBuffer {
    VkBuffer      buffer;
    VmaAllocation allocation;
    uint32_t      vertexCount;
    uint32_t      indexCount;
    VkDeviceSize  indexOffset;
} GeoBuffer;

typedef struct App {
    VkPipelineLayout      pipelineLayout;
    VkPipeline            graphicsPipeline;
    VkDescriptorSetLayout globalSetLayout;
    VkDescriptorPool      descriptorPool;
    GeoBuffer             geo;
} App;

static App app;

static VulkanResult create_geo_buffer(VkContext* ctx, GeoBuffer* out,
    Vertex* vertices, uint32_t vertexCount,
    uint32_t* indices, uint32_t indexCount)
{
    VkDeviceSize vertexSize  = sizeof(Vertex) * vertexCount;
    VkDeviceSize indexSize   = sizeof(uint32_t) * indexCount;
    VkDeviceSize totalSize   = vertexSize + indexSize;

    // staging buffer
    VkBufferCreateInfo stagingBufferInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size  = totalSize,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
    };
    VmaAllocationCreateInfo stagingAllocInfo = {
        .usage = VMA_MEMORY_USAGE_AUTO,
        .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                 VMA_ALLOCATION_CREATE_MAPPED_BIT,
    };
    VkBuffer      stagingBuffer;
    VmaAllocation stagingAllocation;
    VmaAllocationInfo stagingAllocResult;
    VkResult result = vmaCreateBuffer(ctx->allocator, &stagingBufferInfo, &stagingAllocInfo,
        &stagingBuffer, &stagingAllocation, &stagingAllocResult);
    if (result != VK_SUCCESS) {
        LOG_ERROR("vmaCreateBuffer failed for staging buffer. VkResult: %i", result);
        return (VulkanResult){.status = VULKAN_ERROR_BUFFER_CREATION_FAILED, .vk_result = result};
    }

    memcpy(stagingAllocResult.pMappedData, vertices, vertexSize);
    memcpy((char*)stagingAllocResult.pMappedData + vertexSize, indices, indexSize);

    // device-local buffer
    VkBufferCreateInfo deviceBufferInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size  = totalSize,
        .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                 VK_BUFFER_USAGE_INDEX_BUFFER_BIT  |
                 VK_BUFFER_USAGE_TRANSFER_DST_BIT,
    };
    VmaAllocationCreateInfo deviceAllocInfo = {
        .usage = VMA_MEMORY_USAGE_AUTO,
        .flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT,
    };
    result = vmaCreateBuffer(ctx->allocator, &deviceBufferInfo, &deviceAllocInfo,
        &out->buffer, &out->allocation, NULL);
    if (result != VK_SUCCESS) {
        LOG_ERROR("vmaCreateBuffer failed for device buffer. VkResult: %i", result);
        vmaDestroyBuffer(ctx->allocator, stagingBuffer, stagingAllocation);
        return (VulkanResult){.status = VULKAN_ERROR_BUFFER_CREATION_FAILED, .vk_result = result};
    }

    // record and submit transfer
    VkCommandBufferAllocateInfo cmdAllocInfo = {
        .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool        = ctx->transferPool,
        .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    VkCommandBuffer cmd;
    result = vkAllocateCommandBuffers(ctx->logicalDevice, &cmdAllocInfo, &cmd);
    if (result != VK_SUCCESS) {
        LOG_ERROR("vkAllocateCommandBuffers failed for transfer. VkResult: %i", result);
        vmaDestroyBuffer(ctx->allocator, stagingBuffer, stagingAllocation);
        vmaDestroyBuffer(ctx->allocator, out->buffer, out->allocation);
        return (VulkanResult){.status = VULKAN_ERROR_COMMAND_BUFFER_ALLOCATION_FAILED, .vk_result = result};
    }

    VkCommandBufferBeginInfo beginInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vkBeginCommandBuffer(cmd, &beginInfo);

    VkBufferCopy region = {
        .srcOffset = 0,
        .dstOffset = 0,
        .size      = totalSize,
    };
    vkCmdCopyBuffer(cmd, stagingBuffer, out->buffer, 1, &region);
    vkEndCommandBuffer(cmd);

    VkSubmitInfo submitInfo = {
        .sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers    = &cmd,
    };
    vkQueueSubmit(ctx->queues.transfer, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(ctx->queues.transfer);

    vkFreeCommandBuffers(ctx->logicalDevice, ctx->transferPool, 1, &cmd);
    vmaDestroyBuffer(ctx->allocator, stagingBuffer, stagingAllocation);

    out->vertexCount = vertexCount;
    out->indexCount  = indexCount;
    out->indexOffset = vertexSize;

    return (VulkanResult){.status = VULKAN_SUCCESS, .vk_result = VK_SUCCESS};
}

char* read_file(const char* filename, size_t* outSize) {
    FILE* file = fopen(filename, "rb");
    if (!file) {
        fprintf(stderr, "Failed to open file: %s\n", filename);
        return NULL;
    }

    fseek(file, 0, SEEK_END);
    long fileSize = ftell(file);
    if (fileSize < 0) {
        fprintf(stderr, "Failed to determine size of file: %s\n", filename);
        fclose(file);
        return NULL;
    }
    
    fseek(file, 0, SEEK_SET);

    char* buffer = (char*)malloc(fileSize);
    if (!buffer) {
        fprintf(stderr, "Failed to allocate memory for file: %s\n", filename);
        fclose(file);
        return NULL;
    }

    size_t bytesRead = fread(buffer, 1, fileSize, file);
    if (bytesRead != (size_t)fileSize) {
        fprintf(stderr, "Failed to read entire file: %s\n", filename);
        free(buffer);
        fclose(file);
        return NULL;
    }

    fclose(file);
    *outSize = (size_t)fileSize;
    return buffer;
}

VkShaderModule create_shader_module(VkDevice device, const char* code, size_t code_size) {
    VkShaderModuleCreateInfo createInfo = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .codeSize = code_size,
        .pCode = (const uint32_t*)code
    };

    VkShaderModule shaderModule;
    if (vkCreateShaderModule(device, &createInfo, NULL, &shaderModule) != VK_SUCCESS) {
        fprintf(stderr, "Failed to create shader module!\n");
        return VK_NULL_HANDLE;
    }

    return shaderModule;
}

static VulkanResult create_global_descriptor_layout(VkContext* ctx) {
    VkDescriptorSetLayoutBinding bindings[2] = {
        {
            .binding         = 0,
            .descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .descriptorCount = 1,
            .stageFlags      = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        },
        {
            .binding         = 1,
            .descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 1,
            .stageFlags      = VK_SHADER_STAGE_VERTEX_BIT,
        },
    };
    VkDescriptorSetLayoutCreateInfo layoutInfo = {
        .sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 2,
        .pBindings    = bindings,
    };
    VkResult result = vkCreateDescriptorSetLayout(ctx->logicalDevice, &layoutInfo, NULL, &app.globalSetLayout);
    if (result != VK_SUCCESS) {
        LOG_ERROR("vkCreateDescriptorSetLayout failed. VkResult: %i", result);
        return (VulkanResult){.status = VULKAN_ERROR_DESCRIPTOR_SET_LAYOUT_CREATION_FAILED, .vk_result = result};
    }
    return (VulkanResult){.status = VULKAN_SUCCESS, .vk_result = VK_SUCCESS};
}

static VulkanResult create_descriptor_pool(VkContext* ctx) {
    VkDescriptorPoolSize poolSizes[2] = {
        { .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount = MAX_FRAMES_IN_FLIGHT },
        { .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = MAX_FRAMES_IN_FLIGHT },
    };
    VkDescriptorPoolCreateInfo poolInfo = {
        .sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets       = MAX_FRAMES_IN_FLIGHT,
        .poolSizeCount = 2,
        .pPoolSizes    = poolSizes,
    };
    VkResult result = vkCreateDescriptorPool(ctx->logicalDevice, &poolInfo, NULL, &app.descriptorPool);
    if (result != VK_SUCCESS) {
        LOG_ERROR("vkCreateDescriptorPool failed. VkResult: %i", result);
        return (VulkanResult){.status = VULKAN_ERROR_DESCRIPTOR_POOL_CREATION_FAILED, .vk_result = result};
    }
    return (VulkanResult){.status = VULKAN_SUCCESS, .vk_result = VK_SUCCESS};
}

static VulkanResult create_frame_descriptors(VkContext* ctx, VkWindow* window) {
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkDescriptorSetAllocateInfo allocInfo = {
            .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool     = app.descriptorPool,
            .descriptorSetCount = 1,
            .pSetLayouts        = &app.globalSetLayout,
        };
        VkResult result = vkAllocateDescriptorSets(ctx->logicalDevice, &allocInfo, &window->frames[i].globalDescriptorSet);
        if (result != VK_SUCCESS) {
            LOG_ERROR("vkAllocateDescriptorSets failed for frame %u. VkResult: %i", i, result);
            return (VulkanResult){.status = VULKAN_ERROR_DESCRIPTOR_SET_ALLOCATION_FAILED, .vk_result = result};
        }

        VkDescriptorBufferInfo uboInfo = {
            .buffer = window->frames[i].uniformBuffer,
            .offset = 0,
            .range  = sizeof(GlobalUBO),
        };
        VkDescriptorBufferInfo ssboInfo = {
            .buffer = window->frames[i].objectBuffer,
            .offset = 0,
            .range  = sizeof(ObjectSSBO),
        };
        VkWriteDescriptorSet writes[2] = {
            {
                .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet          = window->frames[i].globalDescriptorSet,
                .dstBinding      = 0,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .pBufferInfo     = &uboInfo,
            },
            {
                .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet          = window->frames[i].globalDescriptorSet,
                .dstBinding      = 1,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .pBufferInfo     = &ssboInfo,
            },
        };
        vkUpdateDescriptorSets(ctx->logicalDevice, 2, writes, 0, NULL);
    }
    return (VulkanResult){.status = VULKAN_SUCCESS, .vk_result = VK_SUCCESS};
}

static VulkanResult create_graphics_pipeline(VkContext* ctx, VkWindow* window) {
    size_t shaderSize = 0;
    char* shaderCode = read_file("shaders/slang.spv", &shaderSize);
    
    if (!shaderCode) {
        return (VulkanResult){.status = VULKAN_ERROR_FILE_READ_FAILED, .vk_result = VK_ERROR_UNKNOWN};
    }

    VkShaderModule shaderModule = create_shader_module(ctx->logicalDevice, shaderCode, shaderSize);
    free(shaderCode);

    if (shaderModule == VK_NULL_HANDLE) {
        return (VulkanResult){.status = VULKAN_ERROR_SHADER_MODULE_CREATION_FAILED, .vk_result = VK_ERROR_UNKNOWN};
    }

    VkPipelineShaderStageCreateInfo vertShaderStageInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .stage = VK_SHADER_STAGE_VERTEX_BIT,
        .module = shaderModule,
        .pName = "vertMain",
        .pSpecializationInfo = NULL
    };

    VkPipelineShaderStageCreateInfo fragShaderStageInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
        .module = shaderModule,
        .pName = "fragMain",
        .pSpecializationInfo = NULL
    };

    VkPipelineShaderStageCreateInfo shaderStages[] = { vertShaderStageInfo, fragShaderStageInfo };

    VkVertexInputBindingDescription bindingDesc = {
        .binding   = 0,
        .stride    = sizeof(Vertex),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    };

    VkVertexInputAttributeDescription attrDescs[2] = {
        {
            .location = 0,
            .binding  = 0,
            .format   = VK_FORMAT_R32G32B32_SFLOAT,
            .offset   = offsetof(Vertex, position),
        },
        {
            .location = 1,
            .binding  = 0,
            .format   = VK_FORMAT_R32G32B32_SFLOAT,
            .offset   = offsetof(Vertex, color),
        },
    };

    VkPipelineVertexInputStateCreateInfo vertexInputInfo = {
        .sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount   = 1,
        .pVertexBindingDescriptions      = &bindingDesc,
        .vertexAttributeDescriptionCount = 2,
        .pVertexAttributeDescriptions    = attrDescs,
    };

    VkPipelineInputAssemblyStateCreateInfo inputAssembly = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .primitiveRestartEnable = VK_FALSE
    };

    VkDynamicState dynamicStates[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };
    
    VkPipelineDynamicStateCreateInfo dynamicStateCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .dynamicStateCount = sizeof(dynamicStates) / sizeof(dynamicStates[0]),
        .pDynamicStates = dynamicStates
    };

    VkPipelineViewportStateCreateInfo viewportState = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .viewportCount = 1,
        .pViewports = NULL, 
        .scissorCount = 1,
        .pScissors = NULL   
    };

    VkPipelineRasterizationStateCreateInfo rasterizer = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .depthClampEnable = VK_FALSE,
        .rasterizerDiscardEnable = VK_FALSE,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_BACK_BIT,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .depthBiasEnable = VK_FALSE,
        .depthBiasConstantFactor = 0.0f,
        .depthBiasClamp = 0.0f,
        .depthBiasSlopeFactor = 0.0f,
        .lineWidth = 1.0f
    };

    VkPipelineMultisampleStateCreateInfo multisampling = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
        .sampleShadingEnable = VK_FALSE,
        .minSampleShading = 1.0f,
        .pSampleMask = NULL,
        .alphaToCoverageEnable = VK_FALSE,
        .alphaToOneEnable = VK_FALSE
    };

    VkPipelineColorBlendAttachmentState colorBlendAttachment = {
        .blendEnable = VK_FALSE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ZERO,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | 
                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
    };

    VkPipelineColorBlendStateCreateInfo colorBlending = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .logicOpEnable = VK_FALSE,
        .logicOp = VK_LOGIC_OP_COPY,
        .attachmentCount = 1,
        .pAttachments = &colorBlendAttachment,
        .blendConstants = {0.0f, 0.0f, 0.0f, 0.0f}
    };

    VkPushConstantRange pushConstantRange = {
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
        .offset     = 0,
        .size       = sizeof(uint32_t),
    };

    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {
        .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pNext                  = NULL,
        .flags                  = 0,
        .setLayoutCount         = 1,
        .pSetLayouts            = &app.globalSetLayout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges    = &pushConstantRange,
    };

    VkResult layoutResult = vkCreatePipelineLayout(ctx->logicalDevice, &pipelineLayoutInfo, NULL, &app.pipelineLayout);
    if (layoutResult != VK_SUCCESS) {
        vkDestroyShaderModule(ctx->logicalDevice, shaderModule, NULL);
        return (VulkanResult){.status = VULKAN_ERROR_PIPELINE_LAYOUT_CREATION_FAILED, .vk_result = layoutResult};
    }

    VkPipelineDepthStencilStateCreateInfo depthStencil = {
        .sType                 = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable       = VK_TRUE,
        .depthWriteEnable      = VK_TRUE,
        .depthCompareOp        = VK_COMPARE_OP_LESS,
        .depthBoundsTestEnable = VK_FALSE,
        .stencilTestEnable     = VK_FALSE,
    };

    VkPipelineRenderingCreateInfo pipelineRenderingCreateInfo = {
        .sType                   = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount    = 1,
        .pColorAttachmentFormats = &window->swapChainSurfaceFormat.format,
        .depthAttachmentFormat   = VK_FORMAT_D32_SFLOAT,
    };

    VkGraphicsPipelineCreateInfo pipelineInfo = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &pipelineRenderingCreateInfo,
        .flags = 0,
        .stageCount = 2,
        .pStages = shaderStages,
        .pVertexInputState = &vertexInputInfo,
        .pInputAssemblyState = &inputAssembly,
        .pDepthStencilState = &depthStencil,
        .pTessellationState = NULL,
        .pViewportState = &viewportState,
        .pRasterizationState = &rasterizer,
        .pMultisampleState = &multisampling,
        .pColorBlendState = &colorBlending,
        .pDynamicState = &dynamicStateCreateInfo,
        .layout = app.pipelineLayout,
        .renderPass = VK_NULL_HANDLE,
        .subpass = 0,
        .basePipelineHandle = VK_NULL_HANDLE,
        .basePipelineIndex = -1
    };

    VkResult pipelineResult = vkCreateGraphicsPipelines(ctx->logicalDevice, VK_NULL_HANDLE, 1, &pipelineInfo, NULL, &app.graphicsPipeline);
    vkDestroyShaderModule(ctx->logicalDevice, shaderModule, NULL);
    if (pipelineResult != VK_SUCCESS) {
        return (VulkanResult){.status = VULKAN_ERROR_PIPELINE_CREATION_FAILED, .vk_result = pipelineResult};
    }

    return (VulkanResult){.status = VULKAN_SUCCESS, .vk_result = VK_SUCCESS};
}

void transition_image_layout(
    VkWindow* window,
    VkCommandBuffer          cmd,
    uint32_t                 imageIndex,
    VkImageLayout            old_layout,
    VkImageLayout            new_layout,
    VkAccessFlags2           src_access_mask,
    VkAccessFlags2           dst_access_mask,
    VkPipelineStageFlags2    src_stage_mask,
    VkPipelineStageFlags2    dst_stage_mask)
{
    VkImageMemoryBarrier2 barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .pNext = NULL,
        .srcStageMask = src_stage_mask,
        .srcAccessMask = src_access_mask,
        .dstStageMask = dst_stage_mask,
        .dstAccessMask = dst_access_mask,
        .oldLayout = old_layout,
        .newLayout = new_layout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = window->swapChainImages[imageIndex],
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1
        }
    };

    VkDependencyInfo dependencyInfo = {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .pNext = NULL,
        .dependencyFlags = 0,
        .memoryBarrierCount = 0,
        .pMemoryBarriers = NULL,
        .bufferMemoryBarrierCount = 0,
        .pBufferMemoryBarriers = NULL,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &barrier
    };

    vkCmdPipelineBarrier2(cmd, &dependencyInfo);
}

VulkanResult record_command_buffer(VkWindow* window, uint32_t imageIndex) {
    VkCommandBuffer cmd = window->frames[window->frameIndex].graphicsCommandBuffer;

    VkCommandBufferBeginInfo beginInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .pNext = NULL,
        .flags = 0,
        .pInheritanceInfo = NULL
    };

    VkResult beginResult = vkBeginCommandBuffer(cmd, &beginInfo);
    if (beginResult != VK_SUCCESS) {
        return (VulkanResult){.status = VULKAN_ERROR_COMMAND_BUFFER_FAILED_BEGIN, .vk_result = beginResult};
    }

    transition_image_layout(
        window,
        cmd,
        imageIndex,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        0,                                                  // srcAccessMask
        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,             // dstAccessMask
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,    // srcStageMask
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT     // dstStageMask
    );
    VkImageMemoryBarrier2 depthBarrier = {
        .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask        = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
        .srcAccessMask       = 0,
        .dstStageMask        = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
        .dstAccessMask       = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                               VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        .oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout           = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image               = window->depthImage,
        .subresourceRange    = {
            .aspectMask      = VK_IMAGE_ASPECT_DEPTH_BIT,
            .baseMipLevel    = 0,
            .levelCount      = 1,
            .baseArrayLayer  = 0,
            .layerCount      = 1,
        },
    };
    VkDependencyInfo depthDependency = {
        .sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers    = &depthBarrier,
    };
    vkCmdPipelineBarrier2(cmd, &depthDependency);

    VkClearValue clearColor = {
        .color = { .float32 = {0.1f, 0.1f, 0.1f, 1.0f} }
    };

    VkRenderingAttachmentInfo attachmentInfo = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .pNext = NULL,
        .imageView = window->swapChainImageViews[imageIndex],
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .resolveMode = VK_RESOLVE_MODE_NONE,
        .resolveImageView = VK_NULL_HANDLE,
        .resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue = clearColor
    };

    VkRenderingAttachmentInfo depthAttachmentInfo = {
        .sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView   = window->depthImageView,
        .imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        .loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp     = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .clearValue  = {.depthStencil = {1.0f, 0}},
    };

    VkRenderingInfo renderingInfo = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .pNext = NULL,
        .flags = 0,
        .renderArea = {
            .offset = {0, 0}, 
            .extent = window->swapChainExtent
        },
        .layerCount = 1,
        .viewMask = 0,
        .colorAttachmentCount = 1,
        .pColorAttachments = &attachmentInfo,
        .pDepthAttachment = &depthAttachmentInfo,
        .pStencilAttachment = NULL
    };

    vkCmdBeginRendering(cmd, &renderingInfo);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, app.graphicsPipeline);

    VkViewport viewport = {
        .x = 0.0f,
        .y = 0.0f,
        .width = (float)window->swapChainExtent.width,
        .height = (float)window->swapChainExtent.height,
        .minDepth = 0.0f,
        .maxDepth = 1.0f
    };
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor = {
        .offset = {0, 0},
        .extent = window->swapChainExtent
    };
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &app.geo.buffer, &offset);
    vkCmdBindIndexBuffer(cmd, app.geo.buffer, app.geo.indexOffset, VK_INDEX_TYPE_UINT32);

    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
        app.pipelineLayout, 0, 1,
        &window->frames[window->frameIndex].globalDescriptorSet,
        0, NULL);

    uint32_t objectIndex = 0;
    vkCmdPushConstants(cmd, app.pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT,
        0, sizeof(uint32_t), &objectIndex);

    vkCmdDrawIndexed(cmd, app.geo.indexCount, 1, 0, 0, 0);
    vkCmdEndRendering(cmd);

    transition_image_layout(
        window,
        cmd,
        imageIndex,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,             // srcAccessMask
        0,                                                  // dstAccessMask
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,    // srcStageMask
        VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT              // dstStageMask
    );

    VkResult endResult = vkEndCommandBuffer(cmd);
    if (endResult != VK_SUCCESS) {
        return (VulkanResult){.status = VULKAN_ERROR_COMMAND_BUFFER_FAILED_END, .vk_result = endResult};
    }
    return (VulkanResult){.status = VULKAN_SUCCESS, .vk_result = VK_SUCCESS};
}

static VulkanResult draw_frame(VkContext* ctx, VkWindow* window) {
    uint32_t frameIndex = window->frameIndex;
    VkFrameData* framedata = &window->frames[frameIndex];

    VkResult result = vkWaitForFences(ctx->logicalDevice, 1, &framedata->renderFence, VK_TRUE, UINT64_MAX);
    if (result != VK_SUCCESS) {
        return (VulkanResult){.status = VULKAN_ERROR_FENCE_WAIT_FAILED, .vk_result = result};
    }
    
    uint32_t imageIndex;
    result = vkAcquireNextImageKHR(
        ctx->logicalDevice,
        window->swapChain,
        UINT64_MAX,
        framedata->presentSemaphore,
        VK_NULL_HANDLE,
        &imageIndex
    );
    
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        return (VulkanResult){.status = VULKAN_STATUS_SWAPCHAIN_OUTDATED, .vk_result = result};
    } 
    else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        return (VulkanResult){.status = VULKAN_ERROR_SWAPCHAIN_NEXT_IMAGE_FAILED, .vk_result = result};
    }

    vkResetFences(ctx->logicalDevice, 1, &framedata->renderFence);
    vkResetCommandPool(ctx->logicalDevice, framedata->graphicsPool, 0);
    static double lastTime = 0.0;
    double currentTime = glfwGetTime();
    float deltaTime = (float)(currentTime - lastTime);
    lastTime = currentTime;

    static float angle = 0.0f;
    angle += 1.0f * deltaTime; // 1 radian per second, adjust to taste
    if (angle > 2.0f * GLM_PI) angle -= 2.0f * GLM_PI;
    GlobalUBO ubo = {0};
    vec3 eye    = {0.0f, 0.0f, -3.0f};
    vec3 center = {0.0f, 0.0f,  0.0f};
    vec3 up     = {0.0f, -1.0f, 0.0f};
    glm_lookat(eye, center, up, ubo.view);
    float aspect = (float)window->swapChainExtent.width / (float)window->swapChainExtent.height;
    glm_perspective(glm_rad(60.0f), aspect, 0.1f, 100.0f, ubo.proj);
    ubo.proj[1][1] *= -1.0f;
    ubo.time = angle;
    memcpy(framedata->uniformMapped, &ubo, sizeof(GlobalUBO));

    ObjectSSBO objects = {0};
    glm_mat4_identity(objects.modelMatrices[0]);
    vec3 pos = {0.0f, 0.0f, 2.0f};
    glm_translate(objects.modelMatrices[0], pos);
    vec3 axis = {0.0f, 1.0f, 0.0f};
    glm_rotate(objects.modelMatrices[0], angle, axis);
    memcpy(framedata->objectMapped, &objects, sizeof(ObjectSSBO));
    record_command_buffer(window, imageIndex);

    VkPipelineStageFlags waitDestinationStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submitInfo = {0};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &framedata->presentSemaphore;
    submitInfo.pWaitDstStageMask = &waitDestinationStageMask;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &framedata->graphicsCommandBuffer;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &window->renderSemaphores[imageIndex];

    result = vkQueueSubmit(
        ctx->queues.graphics,
        1,
        &submitInfo,
        framedata->renderFence
    );
    if (result != VK_SUCCESS) {
        return (VulkanResult){.status = VULKAN_ERROR_QUEUE_SUBMIT_FAILED, .vk_result = result};
    }

    VkPresentInfoKHR presentInfo = {0};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &window->renderSemaphores[imageIndex];
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &window->swapChain;
    presentInfo.pImageIndices = &imageIndex;
    presentInfo.pResults = NULL;

    result = vkQueuePresentKHR(ctx->queues.graphics, &presentInfo);
    
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || window->framebufferResized) {
        window->frameIndex = (window->frameIndex + 1) % MAX_FRAMES_IN_FLIGHT;
        return (VulkanResult){.status = VULKAN_STATUS_SWAPCHAIN_OUTDATED, .vk_result = result};
    } else if (result != VK_SUCCESS) {
        return (VulkanResult){.status = VULKAN_ERROR_QUEUE_PRESENT_FAILED, .vk_result = result};
    }

    window->frameIndex = (window->frameIndex + 1) % MAX_FRAMES_IN_FLIGHT;
    return (VulkanResult){.status = VULKAN_SUCCESS, .vk_result = VK_SUCCESS};
}

int main() {
    VkContext context;
    VkContextCreateInfo createInfo = {
        .appName = "Test App",
        .enablePresentation = true,
        .validationLayers = true,
    };

    app.graphicsPipeline  = VK_NULL_HANDLE;
    app.pipelineLayout    = VK_NULL_HANDLE;
    app.globalSetLayout   = VK_NULL_HANDLE;
    app.descriptorPool    = VK_NULL_HANDLE;
    VulkanResult result = vkContextCreate(&createInfo, &context);
    if (result.status != VULKAN_SUCCESS) {
        // blah blah blah
    }

    VkWindow window;
    VkWindowCreateInfo windowInfo = {
        .title = "Test App",
        .height = 600,
        .width = 800,
    };

    result = vkWindowCreate(&context, &windowInfo, &window);
    if (result.status != VULKAN_SUCCESS) {
        // blah blah blah
    }

    result = create_global_descriptor_layout(&context);
    if (result.status != VULKAN_SUCCESS) { }

    result = create_descriptor_pool(&context);
    if (result.status != VULKAN_SUCCESS) { }

    result = create_frame_descriptors(&context, &window);
    if (result.status != VULKAN_SUCCESS) { }

    result = create_geo_buffer(&context, &app.geo,
    boxVertices,  8,
    boxIndices,  36);
    if (result.status != VULKAN_SUCCESS) { }

    result = create_graphics_pipeline(&context, &window);
    if (result.status != VULKAN_SUCCESS) { }

    while(!vkWindowShouldClose(&window)) {
        vkPollEvents();
        result = draw_frame(&context, &window);
        if (result.status == VULKAN_STATUS_SWAPCHAIN_OUTDATED) {
            vkWindowRecreateSwapchain(&context, &window);
        } else if (result.status != VULKAN_SUCCESS) {
            // blah blah blah
        }
    }
    
    vkDeviceWaitIdle(context.logicalDevice);
    vmaDestroyBuffer(context.allocator, app.geo.buffer, app.geo.allocation);
    vkDestroyDescriptorPool(context.logicalDevice, app.descriptorPool, NULL);
    vkDestroyDescriptorSetLayout(context.logicalDevice, app.globalSetLayout, NULL);
    vkDestroyPipeline(context.logicalDevice, app.graphicsPipeline, NULL);
    vkDestroyPipelineLayout(context.logicalDevice, app.pipelineLayout, NULL);
    vkWindowDestroy(&context, &window);
    vkContextDestroy(&context);

    return 0;
}