#include "mesh.h"
#include "logger.h"
#include <string.h>

VkVertexInputBindingDescription vkVertexGetBindingDescription() {
    VkVertexInputBindingDescription bindingDesc = {
        .binding   = 0,
        .stride    = sizeof(Vertex),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    };
    return bindingDesc;
}

void vkVertexGetAttributeDescription(VkVertexInputAttributeDescription attributes[4]) {
    // position
    attributes[0].location = 0;
    attributes[0].binding  = 0;
    attributes[0].format   = VK_FORMAT_R32G32B32_SFLOAT;
    attributes[0].offset   = offsetof(Vertex, position);

    // color
    attributes[1].location = 1;
    attributes[1].binding  = 0;
    attributes[1].format   = VK_FORMAT_R32G32B32_SFLOAT;
    attributes[1].offset   = offsetof(Vertex, color);

    // normal 
    attributes[2].location = 2;
    attributes[2].binding  = 0;
    attributes[2].format   = VK_FORMAT_R32G32B32_SFLOAT;
    attributes[2].offset   = offsetof(Vertex, normal);

    // uv
    attributes[3].location = 3;
    attributes[3].binding  = 0;
    attributes[3].format   = VK_FORMAT_R32G32_SFLOAT;
    attributes[3].offset   = offsetof(Vertex, uv);
}

VulkanResult vkMeshCreate(VkContext* ctx, VkMeshCreateInfo* createInfo, VkMesh* outMesh) {
    VkDeviceSize vertexSize = sizeof(Vertex) * createInfo->vertexCount;
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

    return (VulkanResult){.status = VULKAN_SUCCESS, .vk_result = VK_SUCCESS};
}

void vkMeshDraw(VkMesh* mesh, VkCommandBuffer cmd, uint32_t firstInstance) {
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &mesh->buffer.buffer, &offset);
    vkCmdBindIndexBuffer(cmd, mesh->buffer.buffer, mesh->indexOffset, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmd, mesh->indexCount, 1, 0, 0, firstInstance);
}

void vkMeshDestroy(VkContext* ctx, VkMesh* mesh) {
    vkBufferDestroy(ctx, &mesh->buffer);
}