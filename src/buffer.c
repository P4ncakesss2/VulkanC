#include "buffer.h"
#include "logger.h"
#include "vulkan_ctx.h"

VulkanResult vkBufferCreate(VkContext* ctx, VkDeviceSize size, VkBufferUsageFlags usage, VmaAllocationCreateFlags flags, VulkanBuffer* outBuffer) {
    outBuffer->size = size;
    outBuffer->mappedData = NULL;

    VkBufferCreateInfo bufferInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE
    };

    VmaAllocationCreateInfo allocInfo = {
        .usage = VMA_MEMORY_USAGE_AUTO,
        .flags = flags
    };

    VkResult result = vmaCreateBuffer(ctx->allocator, &bufferInfo, &allocInfo, &outBuffer->buffer, &outBuffer->allocation, NULL);
    if (result != VK_SUCCESS) {
        LOG_ERROR("Failed to allocate buffer via VMA. VkResult: %d", result);
        return (VulkanResult){.status = -2, .vk_result = result};
    }

    if (flags & VMA_ALLOCATION_CREATE_MAPPED_BIT) {
        VmaAllocationInfo allocInfoOut;
        vmaGetAllocationInfo(ctx->allocator, outBuffer->allocation, &allocInfoOut);
        outBuffer->mappedData = allocInfoOut.pMappedData;
    }

    return (VulkanResult){.status = 0, .vk_result = VK_SUCCESS};
}

void vkBufferDestroy(VkContext* ctx, VulkanBuffer* buffer) {
    if (buffer->buffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(ctx->allocator, buffer->buffer, buffer->allocation);
        buffer->buffer = VK_NULL_HANDLE;
        buffer->allocation = VK_NULL_HANDLE;
        buffer->mappedData = NULL;
    }
}