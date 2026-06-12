#ifndef BUFFER_H
#define BUFFER_H

#include <vulkan/vulkan.h>
#include "vk_mem_alloc.h"

typedef struct VkContext VkContext;

typedef struct VulkanBuffer {
    VkBuffer buffer;
    VmaAllocation allocation;
    VkDeviceSize size;
    void* mappedData;
} VulkanBuffer;

typedef struct VulkanResult VulkanResult;

VulkanResult vkBufferCreate(VkContext* ctx, VkDeviceSize size, VkBufferUsageFlags usage, VmaAllocationCreateFlags flags, VulkanBuffer* outBuffer);
void vkBufferDestroy(VkContext* ctx, VulkanBuffer* buffer);

#endif