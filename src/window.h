#ifndef WINDOW_H
#define WINDOW_H

#include "vulkan_ctx.h"
#include <vulkan/vulkan.h>
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <stdint.h>

#define MAX_FRAMES_IN_FLIGHT 2

typedef struct VkFrameData {
    VkCommandPool graphicsPool;
    VkCommandBuffer graphicsCommandBuffer;

    VkCommandPool transferPool;
    VkCommandBuffer transferCommandBuffer;
    
    VkCommandPool computePool;
    VkCommandBuffer computeCommandBuffer;
    
    VkSemaphore presentSemaphore;
    VkSemaphore renderSemaphore;
    VkFence renderFence;
} VkFrameData;

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
    
    uint32_t currentFrameIndex;
    VkFrameData frames[MAX_FRAMES_IN_FLIGHT];
    bool isInitialized;
} VkWindow;

typedef struct VkWindowCreateInfo {
    int width;
    int height;
    const char* title;
} VkWindowCreateInfo;

VulkanResult vkWindowCreate(VkContext* ctx, const VkWindowCreateInfo* createInfo, VkWindow* outWindow);
bool vkWindowShouldClose(VkWindow* window);
void vkPollEvents();
void vkWindowDestroy(VkContext* ctx, VkWindow* window);

#endif