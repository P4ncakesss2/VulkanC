#include "mesh.h"
#include "logger.h"
#include <string.h>

VkVertexInputBindingDescription vkVertexGetBindingDescription(void) {
    return (VkVertexInputBindingDescription){
        .binding   = 0,
        .stride    = sizeof(Vertex),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    };
}

void vkVertexGetAttributeDescription(VkVertexInputAttributeDescription attributes[4]) {
    attributes[0] = (VkVertexInputAttributeDescription){ .location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(Vertex, position) };
    attributes[1] = (VkVertexInputAttributeDescription){ .location = 1, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(Vertex, color)    };
    attributes[2] = (VkVertexInputAttributeDescription){ .location = 2, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(Vertex, normal)   };
    attributes[3] = (VkVertexInputAttributeDescription){ .location = 3, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT,    .offset = offsetof(Vertex, uv)       };
}

void calculate_mesh_bounds(const Vertex* vertices, uint32_t vertexCount, vec3 out_min, vec3 out_max) {
    if (vertexCount == 0) {
        glm_vec3_zero(out_min);
        glm_vec3_zero(out_max);
        return;
    }

    glm_vec3_copy(vertices[0].position, out_min);
    glm_vec3_copy(vertices[0].position, out_max);

    for (uint32_t i = 1; i < vertexCount; i++) {
        out_min[0] = fminf(out_min[0], vertices[i].position[0]);
        out_min[1] = fminf(out_min[1], vertices[i].position[1]);
        out_min[2] = fminf(out_min[2], vertices[i].position[2]);

        out_max[0] = fmaxf(out_max[0], vertices[i].position[0]);
        out_max[1] = fmaxf(out_max[1], vertices[i].position[1]);
        out_max[2] = fmaxf(out_max[2], vertices[i].position[2]);
    }
}

VulkanResult vkMeshCreate(VkContext* ctx, VkMeshCreateInfo* createInfo, VkMesh* outMesh) {
    VkDeviceSize vertexSize = sizeof(Vertex)   * createInfo->vertexCount;
    VkDeviceSize indexSize  = sizeof(uint32_t) * createInfo->indexCount;
    VkDeviceSize totalSize  = vertexSize + indexSize;

    VulkanBuffer stagingBuffer;
    VulkanResult res = vkBufferCreate(
        ctx, totalSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
        &stagingBuffer
    );
    if (res.status != VULKAN_SUCCESS) return res;

    memcpy(stagingBuffer.mappedData, createInfo->vertexArray, vertexSize);
    memcpy((char*)stagingBuffer.mappedData + vertexSize, createInfo->indexArray, indexSize);

    res = vkBufferCreate(
        ctx, totalSize,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT,
        &outMesh->buffer
    );
    if (res.status != VULKAN_SUCCESS) {
        vkBufferDestroy(ctx, &stagingBuffer);
        return res;
    }

    VkCommandBufferAllocateInfo cmdAllocInfo = {
        .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool        = ctx->transferPool,
        .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    VkCommandBuffer cmd;
    VkResult vkRes = vkAllocateCommandBuffers(ctx->logicalDevice, &cmdAllocInfo, &cmd);
    if (vkRes != VK_SUCCESS) {
        LOG_ERROR("vkAllocateCommandBuffers failed for transfer. VkResult: %i", vkRes);
        vkBufferDestroy(ctx, &stagingBuffer);
        vkBufferDestroy(ctx, &outMesh->buffer);
        return (VulkanResult){.status = VULKAN_ERROR_COMMAND_BUFFER_ALLOCATION_FAILED, .vk_result = vkRes};
    }

    VkCommandBufferBeginInfo beginInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vkBeginCommandBuffer(cmd, &beginInfo);
    VkBufferCopy region = { .srcOffset = 0, .dstOffset = 0, .size = totalSize };
    vkCmdCopyBuffer(cmd, stagingBuffer.buffer, outMesh->buffer.buffer, 1, &region);
    vkEndCommandBuffer(cmd);

    VkSubmitInfo submitInfo = {
        .sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers    = &cmd,
    };
    vkQueueSubmit(ctx->queues.transfer, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(ctx->queues.transfer);
    vkFreeCommandBuffers(ctx->logicalDevice, ctx->transferPool, 1, &cmd);

    vkBufferDestroy(ctx, &stagingBuffer);

    outMesh->vertexCount = createInfo->vertexCount;
    outMesh->indexCount  = createInfo->indexCount;
    outMesh->indexOffset = vertexSize;

    calculate_mesh_bounds(createInfo->vertexArray, createInfo->vertexCount, outMesh->local_min, outMesh->local_max);

    return (VulkanResult){.status = VULKAN_SUCCESS, .vk_result = VK_SUCCESS};
}

void vkMeshBind(VkMesh* mesh, VkCommandBuffer cmd) {
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &mesh->buffer.buffer, &offset);
    vkCmdBindIndexBuffer(cmd, mesh->buffer.buffer, mesh->indexOffset, VK_INDEX_TYPE_UINT32);
}

void vkMeshDraw(VkMesh* mesh, VkCommandBuffer cmd, uint32_t firstInstance) {
    vkCmdDrawIndexed(cmd, mesh->indexCount, 1, 0, 0, firstInstance);
}

void vkMeshDestroy(VkContext* ctx, VkMesh* mesh) {
    vkBufferDestroy(ctx, &mesh->buffer);
}