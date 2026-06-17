#ifndef WINDOW_H
#define WINDOW_H

#include "vulkan_ctx.h"
#include "buffer.h"
#include <vulkan/vulkan.h>
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <stdint.h>

typedef struct VkFrameData {
    VkSemaphore presentSemaphore;
    VkFence renderFence;
} VkFrameData;

typedef struct VkImageData {
    VkCommandPool graphicsPool;
    VkCommandBuffer graphicsCommandBuffer;
    VkCommandPool computePool;
    VkCommandBuffer computeCommandBuffer;
    bool commandBufferRecorded;
} VkImageData;

typedef struct VkWindow {
    GLFWwindow* handle;
    VkSurfaceKHR surface;
    VkSwapchainKHR swapChain;
    uint32_t swapChainImageCount;
    VkImage* swapChainImages;
    VkImageView* swapChainImageViews;
    uint32_t swapChainImageViewCount;
    VkSurfaceFormatKHR swapChainSurfaceFormat;
    VkExtent2D swapChainExtent;
    VkSemaphore* renderSemaphores;
    VkImage depthImage;
    VkImageView depthImageView;
    VmaAllocation depthAllocation;
    VkImage msaaColorImage;
    VkImageView msaaColorImageView;
    VmaAllocation msaaColorAllocation;
    uint32_t frameIndex;
    VkFrameData frames[MAX_FRAMES_IN_FLIGHT];
    VkImageData* imageData;
    bool framebufferResized;
} VkWindow;

typedef struct VkWindowCreateInfo {
    int width;
    int height;
    const char* title;
} VkWindowCreateInfo;

VulkanResult vkWindowCreate(VkContext* ctx, const VkWindowCreateInfo* createInfo, VkWindow* outWindow);
VulkanResult vkWindowRecreateSwapchain(VkContext* ctx, VkWindow* window);
bool vkWindowShouldClose(VkWindow* window);
void vkWindowDestroy(VkContext* ctx, VkWindow* window);

#endif