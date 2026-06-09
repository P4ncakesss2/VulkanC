#include "window.h"
#include "logger.h"
#include <stdlib.h>

static VulkanResult create_surface(VkContext *ctx, VkWindow* window)  {
    if (glfwCreateWindowSurface(ctx->instance, window->handle, NULL, &window->surface) != 0)
    {
        return (VulkanResult){.status = VULKAN_ERROR_SURFACE_CREATION_FAILED, .vk_result = VK_ERROR_UNKNOWN};
    }
    return (VulkanResult){.status = VULKAN_SUCCESS, .vk_result = VK_SUCCESS};
}

static VkSurfaceFormatKHR choose_swap_surface_format(VkSurfaceFormatKHR *availableFormats, uint32_t formatCount)
{
    for (uint32_t i = 0; i < formatCount; i++)
    {
        if (availableFormats[i].format == VK_FORMAT_B8G8R8A8_SRGB &&
            availableFormats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        {
            return availableFormats[i];
        }
    }
    return availableFormats[0];
}

static VkPresentModeKHR choose_swap_present_mode(VkPresentModeKHR *availablePresentModes, uint32_t presentModeCount)
{
    for (uint32_t i = 0; i < presentModeCount; i++)
    {
        if (availablePresentModes[i] == VK_PRESENT_MODE_MAILBOX_KHR)
        {
            return VK_PRESENT_MODE_MAILBOX_KHR;
        }
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

static VkExtent2D choose_swap_extent(const VkSurfaceCapabilitiesKHR *capabilities, GLFWwindow *window)
{
    if (capabilities->currentExtent.width != UINT32_MAX)
    {
        return capabilities->currentExtent;
    }

    int width, height;
    glfwGetFramebufferSize(window, &width, &height);

    VkExtent2D actualExtent = {
        .width = (uint32_t)width,
        .height = (uint32_t)height};

    if (actualExtent.width < capabilities->minImageExtent.width)
    {
        actualExtent.width = capabilities->minImageExtent.width;
    }
    else if (actualExtent.width > capabilities->maxImageExtent.width)
    {
        actualExtent.width = capabilities->maxImageExtent.width;
    }

    if (actualExtent.height < capabilities->minImageExtent.height)
    {
        actualExtent.height = capabilities->minImageExtent.height;
    }
    else if (actualExtent.height > capabilities->maxImageExtent.height)
    {
        actualExtent.height = capabilities->maxImageExtent.height;
    }

    return actualExtent;
}

static uint32_t choose_swap_min_image_count(const VkSurfaceCapabilitiesKHR *surfaceCapabilities)
{
    uint32_t minImageCount = (3 > surfaceCapabilities->minImageCount) ? 3 : surfaceCapabilities->minImageCount;
    if ((surfaceCapabilities->maxImageCount > 0) && (surfaceCapabilities->maxImageCount < minImageCount))
    {
        minImageCount = surfaceCapabilities->maxImageCount;
    }
    return minImageCount;
}

static VulkanResult create_swapchain(VkContext* ctx, VkWindow* window, VkSwapchainKHR oldSwapchain) {
    VkSurfaceCapabilitiesKHR surfaceCapabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(ctx->physicalDevice, window->surface, &surfaceCapabilities);

    window->swapChainExtent = choose_swap_extent(&surfaceCapabilities, window->handle);
    uint32_t minImageCount = choose_swap_min_image_count(&surfaceCapabilities);

    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(ctx->physicalDevice, window->surface, &formatCount, NULL);
    VkSurfaceFormatKHR *availableFormats = malloc(formatCount * sizeof(VkSurfaceFormatKHR));
    if (availableFormats == NULL)
    {
        return (VulkanResult){.status = VULKAN_ERROR_OUT_OF_MEMORY, .vk_result = VK_SUCCESS};
    }
    vkGetPhysicalDeviceSurfaceFormatsKHR(ctx->physicalDevice, window->surface, &formatCount, availableFormats);
    window->swapChainSurfaceFormat = choose_swap_surface_format(availableFormats, formatCount);
    free(availableFormats);

    uint32_t presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(ctx->physicalDevice, window->surface, &presentModeCount, NULL);
    VkPresentModeKHR *availablePresentModes = malloc(presentModeCount * sizeof(VkPresentModeKHR));
    if (availablePresentModes == NULL)
    {
        return (VulkanResult){.status = VULKAN_ERROR_OUT_OF_MEMORY, .vk_result = VK_SUCCESS};
    }
    vkGetPhysicalDeviceSurfacePresentModesKHR(ctx->physicalDevice, window->surface, &presentModeCount, availablePresentModes);
    VkPresentModeKHR presentMode = choose_swap_present_mode(availablePresentModes, presentModeCount);
    free(availablePresentModes);

    VkSwapchainCreateInfoKHR swapChainCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .pNext = NULL,
        .flags = 0,
        .surface = window->surface,
        .minImageCount = minImageCount,
        .imageFormat = window->swapChainSurfaceFormat.format,
        .imageColorSpace = window->swapChainSurfaceFormat.colorSpace,
        .imageExtent = window->swapChainExtent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = NULL,
        .preTransform = surfaceCapabilities.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = presentMode,
        .clipped = VK_TRUE,
        .oldSwapchain = oldSwapchain
    };

    VkSwapchainKHR newSwapChain;
    VkResult create_result = vkCreateSwapchainKHR(ctx->logicalDevice, &swapChainCreateInfo, NULL, &newSwapChain);
    if (create_result != VK_SUCCESS)
    {
        return (VulkanResult){.status = VULKAN_ERROR_SWAPCHAIN_CREATION_FAILED, .vk_result = create_result};
    }
    if (oldSwapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(ctx->logicalDevice, oldSwapchain, NULL);
    }

    window->swapChain = newSwapChain;

    vkGetSwapchainImagesKHR(ctx->logicalDevice, window->swapChain, &window->swapChainImageCount, NULL);
    window->swapChainImages = malloc(window->swapChainImageCount * sizeof(VkImage));
    if (window->swapChainImages == NULL)
    {
        return (VulkanResult){.status = VULKAN_ERROR_OUT_OF_MEMORY, .vk_result = VK_SUCCESS};
    }
    vkGetSwapchainImagesKHR(ctx->logicalDevice, window->swapChain, &window->swapChainImageCount, window->swapChainImages);

    return (VulkanResult){.status = VULKAN_SUCCESS, .vk_result = VK_SUCCESS};
}

static VulkanResult create_image_views(VkContext* ctx, VkWindow* window)
{
    window->swapChainImageViewCount = window->swapChainImageCount;
    window->swapChainImageViews = malloc(window->swapChainImageViewCount * sizeof(VkImageView));
    if (window->swapChainImageViews == NULL)
    {
        return (VulkanResult){.status = VULKAN_ERROR_OUT_OF_MEMORY, .vk_result = VK_SUCCESS};
    }

    for (uint32_t i = 0; i < window->swapChainImageCount; i++)
    {
        VkImageViewCreateInfo createInfo = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .pNext = NULL,
            .flags = 0,
            .image = window->swapChainImages[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = window->swapChainSurfaceFormat.format,
            .components = {
                .r = VK_COMPONENT_SWIZZLE_IDENTITY,
                .g = VK_COMPONENT_SWIZZLE_IDENTITY,
                .b = VK_COMPONENT_SWIZZLE_IDENTITY,
                .a = VK_COMPONENT_SWIZZLE_IDENTITY
            },
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1
            }
        };

        VkResult result = vkCreateImageView(ctx->logicalDevice, &createInfo, NULL, &window->swapChainImageViews[i]);
        if (result != VK_SUCCESS)
        {
            return (VulkanResult){.status = VULKAN_ERROR_IMAGE_VIEW_CREATION_FAILED, .vk_result = result};
        }
    }

    return (VulkanResult){.status = VULKAN_SUCCESS, .vk_result = VK_SUCCESS};
}

static void cleanup_swapchain(VkContext* ctx, VkWindow* window) {
    if (window->swapChainImageViews != NULL) {
        for (uint32_t i = 0; i < window->swapChainImageViewCount; i++) {
            if (window->swapChainImageViews[i] != VK_NULL_HANDLE) {
                vkDestroyImageView(ctx->logicalDevice, window->swapChainImageViews[i], NULL);
            }
        }
        free(window->swapChainImageViews);
        window->swapChainImageViews = NULL;
    }
    if (window->swapChainImages != NULL) {
        free(window->swapChainImages);
        window->swapChainImages = NULL;
    }
    if (window->swapChain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(ctx->logicalDevice, window->swapChain, NULL);
        window->swapChain = VK_NULL_HANDLE;
    }
}

VulkanResult vkWindowCreate(VkContext* ctx, const VkWindowCreateInfo* createInfo, VkWindow* outWindow) {
    if (!ctx->presentationEnabled) {
        LOG_ERROR("Cannot create a window! The VulkanContext was initialized with presentationEnabled = false.");
        return (VulkanResult){.status = VULKAN_ERROR_PRESENTATION_NOT_ENABLED, .vk_result = VK_ERROR_UNKNOWN};
    }

    outWindow->handle = NULL;
    outWindow->surface = VK_NULL_HANDLE;

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    
    outWindow->handle = glfwCreateWindow(
        createInfo->width,
        createInfo->height,
        createInfo->title,
        NULL, NULL
    );
    
    if (outWindow->handle == NULL) {
        LOG_ERROR("GLFW Window creation failed.");
        return (VulkanResult){.status = VULKAN_ERROR_WINDOW_CREATION_FAILED, .vk_result = VK_ERROR_UNKNOWN};
    }
    
    glfwSetWindowUserPointer(outWindow->handle, outWindow);
    
    VulkanResult result = create_surface(ctx, outWindow);
    if (result.status != VULKAN_SUCCESS) {
        LOG_ERROR("Vulkan Surface creation failed.");
        vkWindowDestroy(ctx, outWindow);
        return result;
    }
    
    result = vkContextInitializeHardware(ctx, outWindow->surface);
    if (result.status != VULKAN_SUCCESS) {
        LOG_ERROR("Hardware initialization failed during window creation.");
        vkWindowDestroy(ctx, outWindow);
        return result;
    }
    result = create_swapchain(ctx, outWindow, NULL);
    if (result.status != VULKAN_SUCCESS) {
        LOG_ERROR("Vulkan swapchain creation failed.");
        vkWindowDestroy(ctx, outWindow);
        return result;
    }
    result = create_image_views(ctx, outWindow);
    if (result.status != VULKAN_SUCCESS) {
        LOG_ERROR("Vulkan image view creation failed.");
        vkWindowDestroy(ctx, outWindow);
        return result;
    }
    return (VulkanResult){.status = VULKAN_SUCCESS, .vk_result = VK_SUCCESS};
}

bool vkWindowShouldClose(VkWindow* window) {
    if (window->handle == NULL)
        return true;
    return glfwWindowShouldClose(window->handle);
}

void vkPollEvents() {
    glfwPollEvents();
}

void vkWindowDestroy(VkContext* ctx, VkWindow* window) {
    if (window == NULL) return;

    cleanup_swapchain(ctx, window);

    if (window->surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(ctx->instance, window->surface, NULL);
        window->surface = VK_NULL_HANDLE;
    }

    if (window->handle != NULL) {
        glfwDestroyWindow(window->handle);
        window->handle = NULL;
    }
}