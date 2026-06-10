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
    VULKAN_ERROR_COMMAND_POOL_CREATION_FAILED = -14,
    VULKAN_ERROR_SHADER_MODULE_CREATION_FAILED = -15,
    VULKAN_ERROR_FILE_READ_FAILED = -16,
    VULKAN_ERROR_PIPELINE_LAYOUT_CREATION_FAILED = -17,
    VULKAN_ERROR_PIPELINE_CREATION_FAILED = -18,
    VULKAN_ERROR_FENCE_WAIT_FAILED = -19,
    VULKAN_ERROR_SWAPCHAIN_NEXT_IMAGE_FAILED = -20,
    VULKAN_ERROR_QUEUE_PRESENT_FAILED = -21,
    VULKAN_ERROR_QUEUE_SUBMIT_FAILED = -22,
    VULKAN_ERROR_COMMAND_BUFFER_FAILED_END = -23,
    VULKAN_ERROR_COMMAND_BUFFER_FAILED_BEGIN = -24,
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
    bool isInitialized;
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