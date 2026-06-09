#ifndef VULKAN_CTX_H
#define VULKAN_CTX_H

#include "vk_mem_alloc.h"
#include <vulkan/vulkan.h>
#include <stdbool.h>
#include <stdint.h>

typedef enum VulkanStatus {
    VULKAN_SUCCESS = 0,
    VULKAN_ERROR_INSTANCE_CREATION_FAILED = -1,
    VULKAN_ERROR_OUT_OF_MEMORY = -2,
    VULKAN_ERROR_EXTENSION_FETCH_FAILED = -3,
    VULKAN_STATUS_EXTENSIONS_UNSUPPORTED = -4,
    VULKAN_ERROR_WINDOW_CREATION_FAILED = -5,
    VULKAN_ERROR_PRESENTATION_NOT_ENABLED = -6,
    VULKAN_ERROR_NO_SUITABLE_GPU = -7,
    VULKAN_ERROR_SURFACE_CREATION_FAILED = -8,
    VULKAN_ERROR_VMA_ALLOCATOR_CREATION_FAILED = -9,
    VULKAN_ERROR_LOGICAL_DEVICE_CREATION_FAILED = -10,
    VULKAN_ERROR_QUEUE_FETCH_FAILED = -11,
    VULKAN_ERROR_SWAPCHAIN_CREATION_FAILED = -12,
    VULKAN_ERROR_IMAGE_VIEW_CREATION_FAILED = -13,
} VulkanStatus;

typedef struct VulkanResult {
    VulkanStatus status;
    VkResult vk_result;
} VulkanResult;

typedef struct VKQueues {
    VkQueue graphics;
    VkQueue compute;
    VkQueue transfer;

    uint32_t graphicsFamilyIndex;
    uint32_t computeFamilyIndex;
    uint32_t transferFamilyIndex;
} VKQueues;

typedef struct VkContext {
    VkInstance instance;
    VkPhysicalDevice physicalDevice;
    VkDevice logicalDevice;
    VkDebugUtilsMessengerEXT debugMessenger;
    VmaAllocator allocator;

    VkCommandPool transferPool;

    VKQueues queues;
    bool presentationEnabled;
} VkContext;

typedef struct VkContextCreateInfo {
    const char* appName; 
    bool validationLayers;
    bool enablePresentation;
} VkContextCreateInfo;

VulkanResult vkContextCreate(VkContextCreateInfo* createInfo, VkContext* outCtx);
VulkanResult vkContextInitializeHardware(VkContext* ctx, VkSurfaceKHR surface);
void vkContextDestroy(VkContext* ctx);

#endif