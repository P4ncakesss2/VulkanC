#include "mesh.h"
#include "vulkan_ctx.h"
#include "window.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "render_types.h"
#include "logger.h"
#include "texture.h"
#include "material.h"

static vec3 cameraPos   = {0.0f, 0.0f, -2.0f};
static vec3 cameraFront = {0.0f, 0.0f, 1.0f};
static vec3 cameraUp    = {0.0f, 1.0f, 0.0f};

static bool firstMouse = true;
static float yaw   = 90.0f;
static float pitch = 0.0f;
static float lastX = 400.0f;
static float lastY = 300.0f;

static Vertex boxVertices[] = {
    {{-0.5f, -0.5f,  0.5f}, {1,1,1}, {0.0f, 0.0f,  1.0f}, {0.0f, 0.0f}},
    {{ 0.5f, -0.5f,  0.5f}, {1,1,1}, {0.0f, 0.0f,  1.0f}, {1.0f, 0.0f}},
    {{ 0.5f,  0.5f,  0.5f}, {1,1,1}, {0.0f, 0.0f,  1.0f}, {1.0f, 1.0f}},
    {{-0.5f,  0.5f,  0.5f}, {1,1,1}, {0.0f, 0.0f,  1.0f}, {0.0f, 1.0f}},

    {{ 0.5f, -0.5f, -0.5f}, {1,1,1}, {0.0f, 0.0f, -1.0f}, {0.0f, 0.0f}},
    {{-0.5f, -0.5f, -0.5f}, {1,1,1}, {0.0f, 0.0f, -1.0f}, {1.0f, 0.0f}},
    {{-0.5f,  0.5f, -0.5f}, {1,1,1}, {0.0f, 0.0f, -1.0f}, {1.0f, 1.0f}},
    {{ 0.5f,  0.5f, -0.5f}, {1,1,1}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f}},

    {{-0.5f, -0.5f, -0.5f}, {1,1,1}, {-1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},
    {{-0.5f, -0.5f,  0.5f}, {1,1,1}, {-1.0f, 0.0f, 0.0f}, {1.0f, 0.0f}},
    {{-0.5f,  0.5f,  0.5f}, {1,1,1}, {-1.0f, 0.0f, 0.0f}, {1.0f, 1.0f}},
    {{-0.5f,  0.5f, -0.5f}, {1,1,1}, {-1.0f, 0.0f, 0.0f}, {0.0f, 1.0f}},

    {{ 0.5f, -0.5f,  0.5f}, {1,1,1}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},
    {{ 0.5f, -0.5f, -0.5f}, {1,1,1}, {1.0f, 0.0f, 0.0f}, {1.0f, 0.0f}},
    {{ 0.5f,  0.5f, -0.5f}, {1,1,1}, {1.0f, 0.0f, 0.0f}, {1.0f, 1.0f}},
    {{ 0.5f,  0.5f,  0.5f}, {1,1,1}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f}},

    {{-0.5f, -0.5f, -0.5f}, {1,1,1}, {0.0f, -1.0f, 0.0f}, {0.0f, 0.0f}},
    {{ 0.5f, -0.5f, -0.5f}, {1,1,1}, {0.0f, -1.0f, 0.0f}, {1.0f, 0.0f}},
    {{ 0.5f, -0.5f,  0.5f}, {1,1,1}, {0.0f, -1.0f, 0.0f}, {1.0f, 1.0f}},
    {{-0.5f, -0.5f,  0.5f}, {1,1,1}, {0.0f, -1.0f, 0.0f}, {0.0f, 1.0f}},

    {{-0.5f,  0.5f,  0.5f}, {1,1,1}, {0.0f,  1.0f, 0.0f}, {0.0f, 0.0f}},
    {{ 0.5f,  0.5f,  0.5f}, {1,1,1}, {0.0f,  1.0f, 0.0f}, {1.0f, 0.0f}},
    {{ 0.5f,  0.5f, -0.5f}, {1,1,1}, {0.0f,  1.0f, 0.0f}, {1.0f, 1.0f}},
    {{-0.5f,  0.5f, -0.5f}, {1,1,1}, {0.0f,  1.0f, 0.0f}, {0.0f, 1.0f}},
};
 
static uint32_t boxIndices[] = {
     0,  1,  2,   2,  3,  0,
     4,  5,  6,   6,  7,  4,
     8,  9, 10,  10, 11,  8,
    12, 13, 14,  14, 15, 12,
    16, 17, 18,  18, 19, 16,
    20, 21, 22,  22, 23, 20,
};

void generate_sphere(float radius, uint32_t rings, uint32_t sectors, Vertex** outVertices, uint32_t* outVertexCount, uint32_t** outIndices, uint32_t* outIndexCount) {
    uint32_t vertexCount = (rings + 1) * (sectors + 1);
    uint32_t indexCount = rings * sectors * 6;

    Vertex* vertices = (Vertex*)malloc(sizeof(Vertex) * vertexCount);
    uint32_t* indices = (uint32_t*)malloc(sizeof(uint32_t) * indexCount);

    float const R = 1.0f / (float)rings;
    float const S = 1.0f / (float)sectors;
    uint32_t vIdx = 0;

    for (uint32_t r = 0; r <= rings; r++) {
        for (uint32_t s = 0; s <= sectors; s++) {
            float y = sinf(-GLM_PI_2 + GLM_PI * r * R);
            float x = cosf(2 * GLM_PI * s * S) * sinf(GLM_PI * r * R);
            float z = sinf(2 * GLM_PI * s * S) * sinf(GLM_PI * r * R);

            vertices[vIdx].position[0] = x * radius;
            vertices[vIdx].position[1] = y * radius;
            vertices[vIdx].position[2] = z * radius;

            vertices[vIdx].normal[0] = x;
            vertices[vIdx].normal[1] = y;
            vertices[vIdx].normal[2] = z;

            vertices[vIdx].color[0] = 1.0f;
            vertices[vIdx].color[1] = 1.0f;
            vertices[vIdx].color[2] = 1.0f;

            vertices[vIdx].uv[0] = s * S;
            vertices[vIdx].uv[1] = r * R;

            vIdx++;
        }
    }

    uint32_t iIdx = 0;
    for (uint32_t r = 0; r < rings; r++) {
        for (uint32_t s = 0; s < sectors; s++) {
            uint32_t v0 = r * (sectors + 1) + s;
            uint32_t v1 = v0 + 1;
            uint32_t v2 = (r + 1) * (sectors + 1) + s;
            uint32_t v3 = v2 + 1;

            indices[iIdx++] = v0;
            indices[iIdx++] = v1;
            indices[iIdx++] = v2;

            indices[iIdx++] = v1;
            indices[iIdx++] = v3;
            indices[iIdx++] = v2;
        }
    }

    *outVertices = vertices;
    *outVertexCount = vertexCount;
    *outIndices = indices;
    *outIndexCount = indexCount;
}

#define MESH_AMOUNT 5

typedef struct App {
    VkMaterial boxMaterial;
    VkMaterial sphereMaterial;
    VkMaterial skyboxMaterial;
    VkMesh     boxMesh;
    VkMesh     sphereMesh;
    uint32_t   boxtexid;
    uint32_t   spheretexid;
    uint32_t   skyboxTexId;
} App;

static App app;

VulkanResult record_command_buffer(VkContext* ctx, VkWindow* window, uint32_t imageIndex) {
    uint32_t frameIndex = window->frameIndex;
    VkFrameData* framedata = &window->frames[frameIndex];
    VkImageData* imagedata = &window->imageData[imageIndex];
    VkCommandBuffer cmd = imagedata->graphicsCommandBuffer;

    if (imagedata->commandBufferRecorded) {
        vkResetCommandPool(ctx->logicalDevice, imagedata->graphicsPool, 0);
        imagedata->commandBufferRecorded = false;
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

    VkRenderingAttachmentInfo colorAttachment = {
        .sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView   = window->msaaColorImageView,
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp     = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .resolveMode        = VK_RESOLVE_MODE_AVERAGE_BIT,
        .resolveImageView   = window->swapChainImageViews[imageIndex],
        .resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .clearValue  = { .color = {{ 0.1f, 0.1f, 0.1f, 1.0f }} },
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
        .pColorAttachments = &colorAttachment,
        .pDepthAttachment = &depthAttachmentInfo,
    };

    vkCmdBeginRendering(cmd, &renderingInfo);

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

    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, app.boxMaterial.layout, 0, 1, &ctx->globalDescriptorSets[frameIndex], 0, NULL);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, app.boxMaterial.handle);
    uint32_t instanceCount = 0;
    for (int i = 0; i < MESH_AMOUNT - 1; i++) {
        vkMeshDraw(&app.boxMesh, cmd, instanceCount);
        instanceCount++;
    }

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, app.sphereMaterial.handle);
    vkMeshDraw(&app.sphereMesh, cmd, instanceCount);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, app.skyboxMaterial.handle);
    uint32_t skyboxIndex = MESH_AMOUNT;
    vkCmdDraw(cmd, 3, 1, 0, skyboxIndex);

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

static void mouse_callback(GLFWwindow* window, double xposIn, double yposIn) {
    float xpos = (float)xposIn;
    float ypos = (float)yposIn;

    if (firstMouse) {
        lastX = xpos;
        lastY = ypos;
        firstMouse = false;
    }

    float xoffset = xpos - lastX;
    float yoffset = lastY - ypos;
    lastX = xpos;
    lastY = ypos;

    float sensitivity = 0.1f;
    xoffset *= sensitivity;
    yoffset *= sensitivity;

    yaw   += xoffset;
    pitch += yoffset;

    if (pitch > 89.0f) pitch = 89.0f;
    if (pitch < -89.0f) pitch = -89.0f;

    vec3 front;
    front[0] = cosf(glm_rad(yaw)) * cosf(glm_rad(pitch));
    front[1] = sinf(glm_rad(pitch));
    front[2] = sinf(glm_rad(yaw)) * cosf(glm_rad(pitch));
    glm_vec3_normalize_to(front, cameraFront);
}

static void process_input(GLFWwindow *window, float deltaTime) {
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(window, true);

    float cameraSpeed = 5.0f * deltaTime;
    vec3 temp;
    
    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) {
        glm_vec3_scale(cameraFront, cameraSpeed, temp);
        glm_vec3_add(cameraPos, temp, cameraPos);
    }
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) {
        glm_vec3_scale(cameraFront, cameraSpeed, temp);
        glm_vec3_sub(cameraPos, temp, cameraPos);
    }
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) {
        glm_vec3_cross(cameraFront, cameraUp, temp);
        glm_vec3_normalize(temp);
        glm_vec3_scale(temp, cameraSpeed, temp);
        glm_vec3_sub(cameraPos, temp, cameraPos);
    }
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) {
        glm_vec3_cross(cameraFront, cameraUp, temp);
        glm_vec3_normalize(temp);
        glm_vec3_scale(temp, cameraSpeed, temp);
        glm_vec3_add(cameraPos, temp, cameraPos);
    }
    if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS) {
        glm_vec3_scale(cameraUp, cameraSpeed, temp);
        glm_vec3_add(cameraPos, temp, cameraPos);
    }
    if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) {
        glm_vec3_scale(cameraUp, cameraSpeed, temp);
        glm_vec3_sub(cameraPos, temp, cameraPos);
    }
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
    static double lastTime = 0.0;
    double currentTime = glfwGetTime();
    float deltaTime = (float)(currentTime - lastTime);
    lastTime = currentTime;

    static float angle = 0.0f;
    angle += 1.0f * deltaTime;
    if (angle > 2.0f * GLM_PI) angle -= 2.0f * GLM_PI;
    process_input(window->handle, deltaTime);

    GlobalUBO ubo = {0};
    vec3 center;
    glm_vec3_add(cameraPos, cameraFront, center);
    glm_lookat(cameraPos, center, cameraUp, ubo.view);
    float aspect = (float)window->swapChainExtent.width / (float)window->swapChainExtent.height;
    glm_perspective(glm_rad(60.0f), aspect, 0.1f, 100.0f, ubo.proj);
    ubo.proj[1][1] *= -1.0f;
    glm_mat4_inv(ubo.proj, ubo.invProj);

    ubo.time = (float)glfwGetTime();
    memcpy((char*)ctx->uniformBuffer.mappedData + frameIndex * ctx->uniformFrameStride, &ubo, sizeof(GlobalUBO));

    float totalWidth = (MESH_AMOUNT - 1) * 1.5f;
    float startX = -totalWidth / 2.0f;

    char* baseAddr   = (char*)ctx->objectStorageBuffer.mappedData;
    ObjectSSBO* ssbo = (ObjectSSBO*)(baseAddr + (frameIndex * ctx->objectFrameStride));

    for (int i = 0; i < MESH_AMOUNT; i++) {
        float xPos = startX + (i * 1.5f);
        vec3 pos   = {xPos, 0.0f, 3.0f};
        vec3 axis  = {0.0f, 1.0f, 0.0f};
        mat4 tempMatrix;
        glm_mat4_identity(tempMatrix);
        glm_translate(tempMatrix, pos);
        glm_rotate(tempMatrix, angle + i, axis);
        glm_mat4_copy(tempMatrix, ssbo[i].transform);
        ssbo[i].textureID = app.boxtexid;
    }

    mat4 skyboxMatrix;
    glm_mat4_identity(skyboxMatrix);
    glm_mat4_copy(skyboxMatrix, ssbo[MESH_AMOUNT].transform);
    ssbo[MESH_AMOUNT].textureID = app.skyboxTexId;
    ssbo[MESH_AMOUNT-1].textureID = app.spheretexid;

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
        .validationLayers = false,
        .msaaSamples = VK_SAMPLE_COUNT_8_BIT,
    };

    app.boxMaterial.handle = VK_NULL_HANDLE;
    app.boxMaterial.layout = VK_NULL_HANDLE;
    app.sphereMaterial.handle = VK_NULL_HANDLE;
    app.sphereMaterial.layout = VK_NULL_HANDLE;
    app.skyboxMaterial.handle = VK_NULL_HANDLE;
    app.skyboxMaterial.layout = VK_NULL_HANDLE;
    VulkanResult result = vkContextCreate(&createInfo, &context);
    if (result.status != VULKAN_SUCCESS) {
    }

    VkWindow window;
    VkWindowCreateInfo windowInfo = {
        .title = "Test App",
        .height = 600,
        .width = 800,
    };

    result = vkWindowCreate(&context, &windowInfo, &window);
    if (result.status != VULKAN_SUCCESS) {
    }

    glfwSetCursorPosCallback(window.handle, mouse_callback);
    glfwSetInputMode(window.handle, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    VkTexture boxTex;
    result = vkTextureLoad(&context, "assets/box.jpg", &boxTex, &app.boxtexid);
    if (result.status != VULKAN_SUCCESS) { }
    VkTexture sphereTex;
    result = vkTextureLoad(&context, "assets/metal/Metal049A_2K-JPG_Color.jpg", &sphereTex, &app.spheretexid);
    if (result.status != VULKAN_SUCCESS) { }

    VkTexture skyboxTex;
    const char* skyboxFaces[6] = {
        "assets/skybox/right.jpg",
        "assets/skybox/left.jpg",
        "assets/skybox/top.jpg",
        "assets/skybox/bottom.jpg",
        "assets/skybox/front.jpg",
        "assets/skybox/back.jpg"
    };
    result = vkTextureLoadCubemap(&context, skyboxFaces, &skyboxTex, &app.skyboxTexId);
    if (result.status != VULKAN_SUCCESS) { }
    
    VkMeshCreateInfo boxMeshInfo = {
        .vertexArray  = boxVertices,
        .indexArray   = boxIndices,
        .vertexCount  = sizeof(boxVertices) / sizeof(boxVertices[0]),
        .indexCount   = sizeof(boxIndices)  / sizeof(boxIndices[0]),
    };
    result = vkMeshCreate(&context, &boxMeshInfo, &app.boxMesh);
    if (result.status != VULKAN_SUCCESS) { }

    Vertex* generatedSphereVertices = NULL;
    uint32_t generatedSphereVertexCount = 0;
    uint32_t* generatedSphereIndices = NULL;
    uint32_t generatedSphereIndexCount = 0;

    generate_sphere(0.5f, 20, 20, &generatedSphereVertices, &generatedSphereVertexCount, &generatedSphereIndices, &generatedSphereIndexCount);

    VkMeshCreateInfo sphereMeshInfo = {
        .vertexArray = generatedSphereVertices,
        .indexArray  = generatedSphereIndices,
        .vertexCount = generatedSphereVertexCount,
        .indexCount  = generatedSphereIndexCount,
    };
    result = vkMeshCreate(&context, &sphereMeshInfo, &app.sphereMesh);
    if (result.status != VULKAN_SUCCESS) { }

    free(generatedSphereVertices);
    free(generatedSphereIndices);

    VkShaderStageCreateInfo stages[] = {
        { VK_SHADER_STAGE_VERTEX_BIT,   "shaders/slang.spv", "vertMain" },
        { VK_SHADER_STAGE_FRAGMENT_BIT, "shaders/slang.spv", "fragMain" }
    };

    VkShaderStageCreateInfo skyboxStages[] = {
        { VK_SHADER_STAGE_VERTEX_BIT,   "shaders/skybox.spv", "vertMain" },
        { VK_SHADER_STAGE_FRAGMENT_BIT, "shaders/skybox.spv", "fragMain" }
    };

    VkPushConstantRange pushConstantRange = {
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
        .offset     = 0,
        .size       = sizeof(uint32_t),
    };

    VkDescriptorSetLayout layouts[1] = {
        context.globalSetLayout,
    };

    VkPipelineBuilder boxBuilder = vkPipelineBuilderCreateDefault();
    boxBuilder.stages = stages;
    boxBuilder.stageCount = 2;
    boxBuilder.setLayouts = layouts;
    boxBuilder.setLayoutCount = 1;
    boxBuilder.pushConstants = &pushConstantRange;
    boxBuilder.pushConstantCount = 1;
    boxBuilder.colorFormat = window.swapChainSurfaceFormat.format;
    boxBuilder.depthFormat = VK_FORMAT_D32_SFLOAT;
    boxBuilder.msaaSamples = context.msaaSamples;
    boxBuilder.cullMode = VK_CULL_MODE_BACK_BIT;
    boxBuilder.depthTestEnable = VK_TRUE;
    boxBuilder.depthWriteEnable = VK_TRUE;
    boxBuilder.polygonMode = VK_POLYGON_MODE_FILL;

    result = vkMaterialBuild(&context, &boxBuilder, &app.boxMaterial);
    if (result.status != VULKAN_SUCCESS) { }

    VkPipelineBuilder sphereBuilder = vkPipelineBuilderCreateDefault();
    sphereBuilder.stages = stages;
    sphereBuilder.stageCount = 2;
    sphereBuilder.setLayouts = layouts;
    sphereBuilder.setLayoutCount = 1;
    sphereBuilder.pushConstants = &pushConstantRange;
    sphereBuilder.pushConstantCount = 1;
    sphereBuilder.colorFormat = window.swapChainSurfaceFormat.format;
    sphereBuilder.depthFormat = VK_FORMAT_D32_SFLOAT;
    sphereBuilder.msaaSamples = context.msaaSamples;
    sphereBuilder.cullMode = VK_CULL_MODE_FRONT_BIT;
    sphereBuilder.depthTestEnable = VK_TRUE;
    sphereBuilder.depthWriteEnable = VK_TRUE;
    sphereBuilder.polygonMode = VK_POLYGON_MODE_FILL;

    result = vkMaterialBuild(&context, &sphereBuilder, &app.sphereMaterial);
    if (result.status != VULKAN_SUCCESS) { }

    VkPipelineBuilder skyboxBuilder = vkPipelineBuilderCreateDefault();
    skyboxBuilder.stages = skyboxStages;
    skyboxBuilder.stageCount = 2;
    skyboxBuilder.setLayouts = layouts;
    skyboxBuilder.setLayoutCount = 1;
    skyboxBuilder.pushConstants = &pushConstantRange;
    skyboxBuilder.pushConstantCount = 1;
    skyboxBuilder.colorFormat = window.swapChainSurfaceFormat.format;
    skyboxBuilder.depthFormat = VK_FORMAT_D32_SFLOAT;
    skyboxBuilder.msaaSamples = context.msaaSamples;
    skyboxBuilder.cullMode = VK_CULL_MODE_NONE;
    skyboxBuilder.depthTestEnable = VK_TRUE;
    skyboxBuilder.depthWriteEnable = VK_FALSE;
    skyboxBuilder.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    skyboxBuilder.vertexBindingCount = 0;
    skyboxBuilder.vertexAttributeCount = 0;
    skyboxBuilder.polygonMode = VK_POLYGON_MODE_FILL;

    result = vkMaterialBuild(&context, &skyboxBuilder, &app.skyboxMaterial);
    if (result.status != VULKAN_SUCCESS) { }

    double lastTime = glfwGetTime();
    int frameCount = 0;

    while(!vkWindowShouldClose(&window)) {
        glfwPollEvents();

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
        }
    }
    
    vkDeviceWaitIdle(context.logicalDevice);
    vkTextureDestroy(&context, &boxTex);
    vkTextureDestroy(&context, &sphereTex);
    vkTextureDestroy(&context, &skyboxTex);
    vkMaterialDestroy(&context, &app.boxMaterial);
    vkMaterialDestroy(&context, &app.sphereMaterial);
    vkMaterialDestroy(&context, &app.skyboxMaterial);
    vkMeshDestroy(&context, &app.boxMesh);
    vkMeshDestroy(&context, &app.sphereMesh);
    vkWindowDestroy(&context, &window);
    vkContextDestroy(&context);

    return 0;
}