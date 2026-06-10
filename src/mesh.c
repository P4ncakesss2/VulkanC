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

void vkVertexGetAttributeDescription(VkVertexInputAttributeDescription attributes[2]) {
    attributes[0].location = 0;
    attributes[0].binding = 0;
    attributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributes[0].offset = offsetof(Vertex, position);

    attributes[1].location = 1;
    attributes[1].binding = 0;
    attributes[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributes[1].offset = offsetof(Vertex, color);
}

VulkanResult vkMeshCreate(VkContext* ctx, VkMeshCreateInfo* createInfo, VkMesh* outMesh) {
    VkDeviceSize vertexSize  = sizeof(Vertex) * createInfo->vertexCount;
    VkDeviceSize indexSize   = sizeof(uint32_t) * createInfo->indexCount;
    VkDeviceSize totalSize   = vertexSize + indexSize;

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

    memcpy(stagingAllocResult.pMappedData, createInfo->vertexArray, vertexSize);
    memcpy((char*)stagingAllocResult.pMappedData + vertexSize, createInfo->indexArray, indexSize);

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
        &outMesh->buffer, &outMesh->allocation, NULL);
    if (result != VK_SUCCESS) {
        LOG_ERROR("vmaCreateBuffer failed for device buffer. VkResult: %i", result);
        vmaDestroyBuffer(ctx->allocator, stagingBuffer, stagingAllocation);
        return (VulkanResult){.status = VULKAN_ERROR_BUFFER_CREATION_FAILED, .vk_result = result};
    }

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
        vmaDestroyBuffer(ctx->allocator, outMesh->buffer, outMesh->allocation);
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
    vkCmdCopyBuffer(cmd, stagingBuffer, outMesh->buffer, 1, &region);
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

    outMesh->vertexCount = createInfo->vertexCount;
    outMesh->indexCount  = createInfo->indexCount;
    outMesh->indexOffset = vertexSize;

    return (VulkanResult){.status = VULKAN_SUCCESS, .vk_result = VK_SUCCESS};
}

VulkanResult vkMeshBind(VkMesh* mesh, VkCommandBuffer cmd) {
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &mesh->buffer, &offset);
    vkCmdBindIndexBuffer(cmd, mesh->buffer, mesh->indexOffset, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmd, mesh->indexCount, 1, 0, 0, 0);
}

void vkMeshDestroy(VkContext* ctx, VkMesh* mesh) {
    vmaDestroyBuffer(ctx->allocator, mesh->buffer, mesh->allocation);
}