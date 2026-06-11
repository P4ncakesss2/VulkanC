#include "mesh.h"
#include "vulkan_ctx.h"
#include "window.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "render_types.h"
#include "logger.h"
#include "texture.h"

static Vertex boxVertices[] = {
    // Front  (+Z)
    {{-0.5f, -0.5f,  0.5f}, {1,1,1}, {0.0f, 0.0f}},
    {{ 0.5f, -0.5f,  0.5f}, {1,1,1}, {1.0f, 0.0f}},
    {{ 0.5f,  0.5f,  0.5f}, {1,1,1}, {1.0f, 1.0f}},
    {{-0.5f,  0.5f,  0.5f}, {1,1,1}, {0.0f, 1.0f}},
    // Back   (-Z)
    {{ 0.5f, -0.5f, -0.5f}, {1,1,1}, {0.0f, 0.0f}},
    {{-0.5f, -0.5f, -0.5f}, {1,1,1}, {1.0f, 0.0f}},
    {{-0.5f,  0.5f, -0.5f}, {1,1,1}, {1.0f, 1.0f}},
    {{ 0.5f,  0.5f, -0.5f}, {1,1,1}, {0.0f, 1.0f}},
    // Left   (-X)
    {{-0.5f, -0.5f, -0.5f}, {1,1,1}, {0.0f, 0.0f}},
    {{-0.5f, -0.5f,  0.5f}, {1,1,1}, {1.0f, 0.0f}},
    {{-0.5f,  0.5f,  0.5f}, {1,1,1}, {1.0f, 1.0f}},
    {{-0.5f,  0.5f, -0.5f}, {1,1,1}, {0.0f, 1.0f}},
    // Right  (+X)
    {{ 0.5f, -0.5f,  0.5f}, {1,1,1}, {0.0f, 0.0f}},
    {{ 0.5f, -0.5f, -0.5f}, {1,1,1}, {1.0f, 0.0f}},
    {{ 0.5f,  0.5f, -0.5f}, {1,1,1}, {1.0f, 1.0f}},
    {{ 0.5f,  0.5f,  0.5f}, {1,1,1}, {0.0f, 1.0f}},
    // Top    (+Y)  — note: Y-down in Vulkan, flip if needed
    {{-0.5f, -0.5f, -0.5f}, {1,1,1}, {0.0f, 0.0f}},
    {{ 0.5f, -0.5f, -0.5f}, {1,1,1}, {1.0f, 0.0f}},
    {{ 0.5f, -0.5f,  0.5f}, {1,1,1}, {1.0f, 1.0f}},
    {{-0.5f, -0.5f,  0.5f}, {1,1,1}, {0.0f, 1.0f}},
    // Bottom (-Y)
    {{-0.5f,  0.5f,  0.5f}, {1,1,1}, {0.0f, 0.0f}},
    {{ 0.5f,  0.5f,  0.5f}, {1,1,1}, {1.0f, 0.0f}},
    {{ 0.5f,  0.5f, -0.5f}, {1,1,1}, {1.0f, 1.0f}},
    {{-0.5f,  0.5f, -0.5f}, {1,1,1}, {0.0f, 1.0f}},
};
 
static uint32_t boxIndices[] = {
     0,  1,  2,   2,  3,  0,   // front
     4,  5,  6,   6,  7,  4,   // back
     8,  9, 10,  10, 11,  8,   // left
    12, 13, 14,  14, 15, 12,   // right
    16, 17, 18,  18, 19, 16,   // top
    20, 21, 22,  22, 23, 20,   // bottom
};


#define MESH_AMOUNT 5

typedef struct App {
    VkPipelineLayout      pipelineLayout;
    VkPipeline            graphicsPipeline;
    VkMesh             mesh[MESH_AMOUNT]; // in the future get it out of here
} App;

static App app;

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

    VkVertexInputBindingDescription bindingDesc = vkVertexGetBindingDescription();
    VkVertexInputAttributeDescription attrDescs[3];
    vkVertexGetAttributeDescription(attrDescs);

    VkPipelineVertexInputStateCreateInfo vertexInputInfo = {
        .sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount   = 1,
        .pVertexBindingDescriptions      = &bindingDesc,
        .vertexAttributeDescriptionCount = 3,
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

    VkDescriptorSetLayout layouts[2] = {
        ctx->globalSetLayout,  // set = 0
        ctx->windowSetLayout   // set = 1
    };

    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {
        .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pNext                  = NULL,
        .flags                  = 0,
        .setLayoutCount         = 2,
        .pSetLayouts            = layouts,
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

VulkanResult record_command_buffer(VkContext* ctx, VkWindow* window, uint32_t imageIndex) {
    uint32_t frameIndex = window->frameIndex;
    VkFrameData* framedata = &window->frames[frameIndex];
    VkImageData* imagedata = &window->imageData[imageIndex];
    VkCommandBuffer cmd = imagedata->graphicsCommandBuffer;

    if (imagedata->commandBufferRecorded) {
        return (VulkanResult){.status = VULKAN_SUCCESS, .vk_result = VK_SUCCESS};
    }

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

    VkImageMemoryBarrier2 initialBarriers[2] = {
        {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .pNext = NULL,
            .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            .srcAccessMask = 0,
            .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
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
        },
        {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .pNext = NULL,
            .srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
            .srcAccessMask = 0,
            .dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
            .dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                             VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = window->depthImage,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            }
        }
    };

    VkDependencyInfo initialDependency = {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .pNext = NULL,
        .dependencyFlags = 0,
        .imageMemoryBarrierCount = 2,
        .pImageMemoryBarriers = initialBarriers
    };

    vkCmdPipelineBarrier2(cmd, &initialDependency);

    VkClearValue clearColor = {
        .color = { .float32 = {0.1f, 0.1f, 0.1f, 1.0f} }
    };

    VkRenderingAttachmentInfo attachmentInfo = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .pNext = NULL,
        .imageView = window->swapChainImageViews[imageIndex],
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .resolveMode = VK_RESOLVE_MODE_NONE,
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
        .colorAttachmentCount = 1,
        .pColorAttachments = &attachmentInfo,
        .pDepthAttachment = &depthAttachmentInfo,
    };

    vkCmdBeginRendering(cmd, &renderingInfo);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, app.graphicsPipeline);

    VkViewport viewport = {
        .x = 0.0f, .y = 0.0f,
        .width = (float)window->swapChainExtent.width,
        .height = (float)window->swapChainExtent.height,
        .minDepth = 0.0f, .maxDepth = 1.0f
    };
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor = {
        .offset = {0, 0},
        .extent = window->swapChainExtent
    };
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    VkDescriptorSet descriptorSets[2] = {
        ctx->globalDescriptorSets[frameIndex],
        window->windowDescriptorSets[frameIndex]
    };
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
        app.pipelineLayout, 0, 2, descriptorSets, 0, NULL);
    
    uint32_t instanceCount = 0;
    for (int i=0; i < MESH_AMOUNT; i++) {
        vkMeshDraw(&app.mesh[i], cmd, instanceCount);
        instanceCount++;
    }

    vkCmdEndRendering(cmd);

    VkImageMemoryBarrier2 presentBarrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .pNext = NULL,
        .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        .srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
        .dstAccessMask = 0,
        .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = window->swapChainImages[imageIndex],
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0, .levelCount = 1,
            .baseArrayLayer = 0, .layerCount = 1
        }
    };

    VkDependencyInfo presentDependency = {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &presentBarrier
    };
    vkCmdPipelineBarrier2(cmd, &presentDependency);

    VkResult endResult = vkEndCommandBuffer(cmd);
    if (endResult != VK_SUCCESS) {
        return (VulkanResult){.status = VULKAN_ERROR_COMMAND_BUFFER_FAILED_END, .vk_result = endResult};
    }
    imagedata->commandBufferRecorded = true;
    return (VulkanResult){.status = VULKAN_SUCCESS, .vk_result = VK_SUCCESS};
}

void update_single_transform(VkContext* ctx, uint32_t frameIndex, uint32_t objectIndex, mat4 newMatrix) {
    char* baseAddr = (char*)ctx->objectStorageMapped;
    mat4* matrixArray = (mat4*)(baseAddr + (frameIndex * ctx->objectFrameStride));
    memcpy(&matrixArray[objectIndex], newMatrix, sizeof(mat4));
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

    VkImageData* imagedata = &window->imageData[imageIndex];
    
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        return (VulkanResult){.status = VULKAN_STATUS_SWAPCHAIN_OUTDATED, .vk_result = result};
    } 
    else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        return (VulkanResult){.status = VULKAN_ERROR_SWAPCHAIN_NEXT_IMAGE_FAILED, .vk_result = result};
    }

    vkResetFences(ctx->logicalDevice, 1, &framedata->renderFence);
    //vkResetCommandPool(ctx->logicalDevice, framedata->graphicsPool, 0);
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

    float totalWidth = (MESH_AMOUNT - 1) * 1.5f;
    float startX = -totalWidth / 2.0f;


    for (int i = 0; i < MESH_AMOUNT; i++) {
        float xPos = startX + (i * 1.5f);
        vec3 pos   = {xPos, 0.0f, 3.0f};
        vec3 axis  = {0.0f, 1.0f, 0.0f};
        mat4 tempMatrix;
        glm_mat4_identity(tempMatrix);
        glm_translate(tempMatrix, pos);
        glm_rotate(tempMatrix, angle + i, axis);
    
        // write transform + textureID together
        char* baseAddr   = (char*)ctx->objectStorageMapped;
        ObjectSSBO* ssbo = (ObjectSSBO*)(baseAddr + (frameIndex * ctx->objectFrameStride));
        glm_mat4_copy(tempMatrix, ssbo[i].transform);
        ssbo[i].textureID = app.mesh[i].textureID;
    }

    record_command_buffer(ctx, window, imageIndex);

    VkPipelineStageFlags waitDestinationStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submitInfo = {0};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &framedata->presentSemaphore;
    submitInfo.pWaitDstStageMask = &waitDestinationStageMask;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &imagedata->graphicsCommandBuffer;
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


    VkTexture stoneTex;
    uint32_t  stoneSlot;
    result = vkTextureLoad(&context, "assets/box.jpg", &stoneTex, &stoneSlot);
    if (result.status != VULKAN_SUCCESS) { /* handle */ }
    
    VkMeshCreateInfo meshInfo = {
        .vertexArray  = boxVertices,
        .indexArray   = boxIndices,
        .vertexCount  = sizeof(boxVertices) / sizeof(boxVertices[0]),
        .indexCount   = sizeof(boxIndices)  / sizeof(boxIndices[0]),
        .textureID    = stoneSlot,   // <-- assigned here
    };
    for (int i = 0; i < MESH_AMOUNT; i++) {
        result = vkMeshCreate(&context, &meshInfo, &app.mesh[i]);
        if (result.status != VULKAN_SUCCESS) { /* handle */ }
    }


    result = create_graphics_pipeline(&context, &window);
    if (result.status != VULKAN_SUCCESS) { }

    double lastTime = glfwGetTime();
    int frameCount = 0;

    while(!vkWindowShouldClose(&window)) {
        vkPollEvents();

        double currentTime = glfwGetTime();
        frameCount++;
        
        if (currentTime - lastTime >= 1.0) {
            char titleBuffer[128];
            double fps = (double)frameCount / (currentTime - lastTime);
            
            snprintf(titleBuffer, sizeof(titleBuffer), "Test App - FPS: %.1f", fps);
            glfwSetWindowTitle(window.handle, titleBuffer);
            
            frameCount = 0;
            lastTime = currentTime;
        }

        result = draw_frame(&context, &window); 
        if (result.status == VULKAN_STATUS_SWAPCHAIN_OUTDATED) {
            vkWindowRecreateSwapchain(&context, &window);
        } else if (result.status != VULKAN_SUCCESS) {
            // blah blah blah
        }
    }
    
    vkDeviceWaitIdle(context.logicalDevice);
    vkTextureDestroy(&context, &stoneTex);
    vkDestroyPipeline(context.logicalDevice, app.graphicsPipeline, NULL);
    vkDestroyPipelineLayout(context.logicalDevice, app.pipelineLayout, NULL);

    for(int i=0; i < MESH_AMOUNT; i++) {
        vkMeshDestroy(&context, &app.mesh[i]);
    }
    vkWindowDestroy(&context, &window);
    vkContextDestroy(&context);

    return 0;
}