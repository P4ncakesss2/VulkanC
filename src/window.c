#include "window.h"
#include "logger.h"
#include <stdlib.h>
#include "render_types.h"

static VulkanResult create_surface(VkContext *ctx, VkWindow* window) {
    VkResult result = glfwCreateWindowSurface(ctx->instance, window->handle, NULL, &window->surface);
    if (result != VK_SUCCESS) {
        LOG_ERROR("glfwCreateWindowSurface failed. VkResult: %i", result);
        return (VulkanResult){.status = VULKAN_ERROR_SURFACE_CREATION_FAILED, .vk_result = result};
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

static VulkanResult create_render_semaphores(VkContext* ctx, VkWindow* window) {
    window->renderSemaphores = malloc(window->swapChainImageCount * sizeof(VkSemaphore));
    if (window->renderSemaphores == NULL) {
        LOG_ERROR("Out of memory allocating render semaphores.");
        return (VulkanResult){.status = VULKAN_ERROR_OUT_OF_MEMORY, .vk_result = VK_SUCCESS};
    }
    VkSemaphoreCreateInfo semInfo = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    for (uint32_t i = 0; i < window->swapChainImageCount; i++) {
        window->renderSemaphores[i] = VK_NULL_HANDLE;
        VkResult result = vkCreateSemaphore(ctx->logicalDevice, &semInfo, NULL, &window->renderSemaphores[i]);
        if (result != VK_SUCCESS) {
            LOG_ERROR("vkCreateSemaphore failed for render semaphore %u. VkResult: %i", i, result);
            return (VulkanResult){.status = VULKAN_ERROR_SEMAPHORE_CREATION_FAILED, .vk_result = result};
        }
    }
    return (VulkanResult){.status = VULKAN_SUCCESS, .vk_result = VK_SUCCESS};
}

static VulkanResult create_image_data(VkContext* ctx, VkWindow* window) {
    window->imageData = calloc(window->swapChainImageCount, sizeof(VkImageData));
    for(int i=0; i < window->swapChainImageCount; i++) {
        VkCommandPoolCreateInfo graphicsPoolInfo = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .flags = 0,
            .queueFamilyIndex = ctx->queues.graphicsFamilyIndex
        };
        VkResult result = vkCreateCommandPool(ctx->logicalDevice, &graphicsPoolInfo, NULL, &window->imageData[i].graphicsPool);
        if (result != VK_SUCCESS) {
            LOG_ERROR("vkCreateCommandPool failed for graphics pool (frame %u). VkResult: %i", i, result);
            return (VulkanResult){.status = VULKAN_ERROR_COMMAND_POOL_CREATION_FAILED, .vk_result = result};
        }

        VkCommandPoolCreateInfo computePoolInfo = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .flags = 0,
            .queueFamilyIndex = ctx->queues.computeFamilyIndex
        };
        result = vkCreateCommandPool(ctx->logicalDevice, &computePoolInfo, NULL, &window->imageData[i].computePool);
        if (result != VK_SUCCESS) {
            LOG_ERROR("vkCreateCommandPool failed for compute pool (frame %u). VkResult: %i", i, result);
            return (VulkanResult){.status = VULKAN_ERROR_COMMAND_POOL_CREATION_FAILED, .vk_result = result};
        }

        VkCommandBufferAllocateInfo allocInfo = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = window->imageData[i].graphicsPool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1
        };
        result = vkAllocateCommandBuffers(ctx->logicalDevice, &allocInfo, &window->imageData[i].graphicsCommandBuffer);
        if (result != VK_SUCCESS) {
            LOG_ERROR("vkAllocateCommandBuffers failed for graphics command buffer (frame %u). VkResult: %i", i, result);
            return (VulkanResult){.status = VULKAN_ERROR_COMMAND_BUFFER_ALLOCATION_FAILED, .vk_result = result};
        }
        allocInfo.commandPool = window->imageData[i].computePool;
        result = vkAllocateCommandBuffers(ctx->logicalDevice, &allocInfo, &window->imageData[i].computeCommandBuffer);
        if (result != VK_SUCCESS) {
            LOG_ERROR("vkAllocateCommandBuffers failed for compute command buffer (frame %u). VkResult: %i", i, result);
            return (VulkanResult){.status = VULKAN_ERROR_COMMAND_BUFFER_ALLOCATION_FAILED, .vk_result = result};
        }
    }
}

static VulkanResult create_frame_data(VkContext* ctx, VkWindow* window) {
    VkSemaphoreCreateInfo semaphoreInfo = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
    };
    VkFenceCreateInfo fenceInfo = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT
    };

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkResult result = vkCreateSemaphore(ctx->logicalDevice, &semaphoreInfo, NULL, &window->frames[i].presentSemaphore);
        if (result != VK_SUCCESS) {
            LOG_ERROR("vkCreateSemaphore failed for present semaphore (frame %u). VkResult: %i", i, result);
            return (VulkanResult){.status = VULKAN_ERROR_SEMAPHORE_CREATION_FAILED, .vk_result = result};
        }

        result = vkCreateFence(ctx->logicalDevice, &fenceInfo, NULL, &window->frames[i].renderFence);
        if (result != VK_SUCCESS) {
            LOG_ERROR("vkCreateFence failed for render fence (frame %u). VkResult: %i", i, result);
            return (VulkanResult){.status = VULKAN_ERROR_FENCE_CREATION_FAILED, .vk_result = result};
        }

        VkBufferCreateInfo uboInfo = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size  = sizeof(GlobalUBO),
            .usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        };
        VmaAllocationCreateInfo uboAllocInfo = {
            .usage = VMA_MEMORY_USAGE_AUTO,
            .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                     VMA_ALLOCATION_CREATE_MAPPED_BIT,
        };
        VmaAllocationInfo uboAllocResult;
        result = vmaCreateBuffer(ctx->allocator, &uboInfo, &uboAllocInfo,
            &window->frames[i].uniformBuffer,
            &window->frames[i].uniformAllocation,
            &uboAllocResult);
        if (result != VK_SUCCESS) {
            LOG_ERROR("vmaCreateBuffer failed for uniform buffer (frame %u). VkResult: %i", i, result);
            return (VulkanResult){.status = VULKAN_ERROR_BUFFER_CREATION_FAILED, .vk_result = result};
        }
        window->frames[i].uniformMapped = uboAllocResult.pMappedData;
    }
    return (VulkanResult){.status = VULKAN_SUCCESS, .vk_result = VK_SUCCESS};
}

static VulkanResult create_depth_image(VkContext* ctx, VkWindow* window) {
    VkImageCreateInfo imageInfo = {
        .sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType     = VK_IMAGE_TYPE_2D,
        .format        = VK_FORMAT_D32_SFLOAT,
        .extent        = {
            .width     = window->swapChainExtent.width,
            .height    = window->swapChainExtent.height,
            .depth     = 1,
        },
        .mipLevels     = 1,
        .arrayLayers   = 1,
        .samples       = VK_SAMPLE_COUNT_1_BIT,
        .tiling        = VK_IMAGE_TILING_OPTIMAL,
        .usage         = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    VmaAllocationCreateInfo allocInfo = {
        .usage = VMA_MEMORY_USAGE_AUTO,
        .flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT,
    };
    VkResult result = vmaCreateImage(ctx->allocator, &imageInfo, &allocInfo,
        &window->depthImage, &window->depthAllocation, NULL);
    if (result != VK_SUCCESS) {
        LOG_ERROR("vmaCreateImage failed for depth image. VkResult: %i", result);
        return (VulkanResult){.status = VULKAN_ERROR_IMAGE_CREATION_FAILED, .vk_result = result};
    }

    VkImageViewCreateInfo viewInfo = {
        .sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image            = window->depthImage,
        .viewType         = VK_IMAGE_VIEW_TYPE_2D,
        .format           = VK_FORMAT_D32_SFLOAT,
        .subresourceRange = {
            .aspectMask   = VK_IMAGE_ASPECT_DEPTH_BIT,
            .baseMipLevel = 0,
            .levelCount   = 1,
            .baseArrayLayer = 0,
            .layerCount   = 1,
        },
    };
    result = vkCreateImageView(ctx->logicalDevice, &viewInfo, NULL, &window->depthImageView);
    if (result != VK_SUCCESS) {
        LOG_ERROR("vkCreateImageView failed for depth image. VkResult: %i", result);
        return (VulkanResult){.status = VULKAN_ERROR_IMAGE_VIEW_CREATION_FAILED, .vk_result = result};
    }
    return (VulkanResult){.status = VULKAN_SUCCESS, .vk_result = VK_SUCCESS};
}

static void cleanup_swapchain(VkContext* ctx, VkWindow* window) {
    if (window->imageData != NULL) {
        for (uint32_t i = 0; i < window->swapChainImageCount; i++) {
            if (window->imageData[i].graphicsPool != VK_NULL_HANDLE) {
                vkDestroyCommandPool(ctx->logicalDevice, window->imageData[i].graphicsPool, NULL);
                window->imageData[i].graphicsPool = VK_NULL_HANDLE;
            }
            if (window->imageData[i].computePool != VK_NULL_HANDLE) {
                vkDestroyCommandPool(ctx->logicalDevice, window->imageData[i].computePool, NULL);
                window->imageData[i].computePool = VK_NULL_HANDLE;
            }
        }
        free(window->imageData);
        window->imageData = NULL;
    }
    if (window->depthImageView != VK_NULL_HANDLE) {
        vkDestroyImageView(ctx->logicalDevice, window->depthImageView, NULL);
        window->depthImageView = VK_NULL_HANDLE;
    }
    if (window->depthImage != VK_NULL_HANDLE) {
        vmaDestroyImage(ctx->allocator, window->depthImage, window->depthAllocation);
        window->depthImage     = VK_NULL_HANDLE;
        window->depthAllocation = VK_NULL_HANDLE;
    }
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

static VulkanResult create_descriptor_pool(VkContext* ctx, VkWindow* window) {
    VkDescriptorPoolSize poolSizes[1] = {
        { .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount = MAX_FRAMES_IN_FLIGHT },
    };
    VkDescriptorPoolCreateInfo poolInfo = {
        .sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets       = MAX_FRAMES_IN_FLIGHT,
        .poolSizeCount = 1,
        .pPoolSizes    = poolSizes,
    };
    VkResult result = vkCreateDescriptorPool(ctx->logicalDevice, &poolInfo, NULL, &window->windowDescriptorPool);
    if (result != VK_SUCCESS) {
        LOG_ERROR("vkCreateDescriptorPool failed. VkResult: %i", result);
        return (VulkanResult){.status = VULKAN_ERROR_DESCRIPTOR_POOL_CREATION_FAILED, .vk_result = result};
    }
    return (VulkanResult){.status = VULKAN_SUCCESS, .vk_result = VK_SUCCESS};
}

static VulkanResult create_frame_descriptors(VkContext* ctx, VkWindow* window) {
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkDescriptorSetAllocateInfo allocInfo = {
            .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool     = window->windowDescriptorPool,
            .descriptorSetCount = 1,
            .pSetLayouts        = &ctx->windowSetLayout,
        };
        VkResult result = vkAllocateDescriptorSets(ctx->logicalDevice, &allocInfo, &window->windowDescriptorSets[i]);
        if (result != VK_SUCCESS) {
            LOG_ERROR("vkAllocateDescriptorSets failed for frame %u. VkResult: %i", i, result);
            return (VulkanResult){.status = VULKAN_ERROR_DESCRIPTOR_SET_ALLOCATION_FAILED, .vk_result = result};
        }

        VkDescriptorBufferInfo uboInfo = {
            .buffer = window->frames[i].uniformBuffer,
            .offset = 0,
            .range  = sizeof(GlobalUBO),
        };
        VkWriteDescriptorSet writes[1] = {
            {
                .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet          = window->windowDescriptorSets[i],
                .dstBinding      = 0,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .pBufferInfo     = &uboInfo,
            },
        };
        vkUpdateDescriptorSets(ctx->logicalDevice, 1, writes, 0, NULL);
    }
    return (VulkanResult){.status = VULKAN_SUCCESS, .vk_result = VK_SUCCESS};
}

VulkanResult vkWindowRecreateSwapchain(VkContext* ctx, VkWindow* window) {
    int width = 0, height = 0;
    glfwGetFramebufferSize(window->handle, &width, &height);
    while (width == 0 || height == 0) {
        glfwGetFramebufferSize(window->handle, &width, &height);
        glfwWaitEvents();
    }
    vkDeviceWaitIdle(ctx->logicalDevice);

    if (window->renderSemaphores != NULL) {
        for (uint32_t i = 0; i < window->swapChainImageCount; i++) {
            if (window->renderSemaphores[i] != VK_NULL_HANDLE)
                vkDestroySemaphore(ctx->logicalDevice, window->renderSemaphores[i], NULL);
        }
        free(window->renderSemaphores);
        window->renderSemaphores = NULL;
    }

    VkSwapchainKHR oldSwapchain = window->swapChain;
    window->swapChain = VK_NULL_HANDLE;
    cleanup_swapchain(ctx, window);

    VulkanResult result = create_swapchain(ctx, window, oldSwapchain);
    if (result.status != VULKAN_SUCCESS) {
        LOG_ERROR("Swapchain recreation failed. Status: %i", result.status);
        return result;
    }
    result = create_image_views(ctx, window);
    if (result.status != VULKAN_SUCCESS) {
        LOG_ERROR("Image view recreation failed. Status: %i", result.status);
        return result;
    }
    result = create_depth_image(ctx, window);
    if (result.status != VULKAN_SUCCESS) {
        LOG_ERROR("Depth image creation failed.");
        vkWindowDestroy(ctx, window);
        return result;
    }
    result = create_render_semaphores(ctx, window);
    if (result.status != VULKAN_SUCCESS) {
        LOG_ERROR("Render semaphore recreation failed. Status: %i", result.status);
        return result;
    }

    result = create_image_data(ctx, window);
    if (result.status != VULKAN_SUCCESS) {
        LOG_ERROR("Vulkan image data creation failed.");
        return result;
    }

    window->framebufferResized = false;
    return (VulkanResult){.status = VULKAN_SUCCESS, .vk_result = VK_SUCCESS};
}

static void framebufferResizeCallback(GLFWwindow* handle, int width, int height)
{
    VkWindow* window = (VkWindow*)glfwGetWindowUserPointer(handle);
    window->framebufferResized = true;
}

VulkanResult vkWindowCreate(VkContext* ctx, const VkWindowCreateInfo* createInfo, VkWindow* outWindow) {
    if (outWindow->isInitialized) {
        LOG_WARN("vkWindowCreate called on an already initialized window!");
        return (VulkanResult){.status = VULKAN_SUCCESS, .vk_result = VK_SUCCESS};
    }
    if (!ctx->presentationEnabled) {
        LOG_ERROR("Cannot create a window! The VulkanContext was initialized with presentationEnabled = false.");
        return (VulkanResult){.status = VULKAN_ERROR_PRESENTATION_NOT_ENABLED, .vk_result = VK_ERROR_UNKNOWN};
    }
    outWindow->handle = NULL;
    outWindow->surface = VK_NULL_HANDLE;
    outWindow->swapChainImageCount = 0;
    outWindow->swapChainImages = NULL;
    outWindow->swapChainImageViewCount = 0;
    outWindow->swapChainImageViews = NULL;
    outWindow->frameIndex = 0;
    outWindow->framebufferResized = false;
    outWindow->isInitialized = false;
    outWindow->renderSemaphores = NULL;
    outWindow->imageData = NULL;

    outWindow->depthImage     = VK_NULL_HANDLE;
    outWindow->depthImageView = VK_NULL_HANDLE;
    outWindow->depthAllocation = VK_NULL_HANDLE;
    
    for(uint32_t i=0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        outWindow->frames[i].presentSemaphore = VK_NULL_HANDLE;
        outWindow->frames[i].renderFence = VK_NULL_HANDLE;
        outWindow->frames[i].uniformBuffer     = VK_NULL_HANDLE;
        outWindow->frames[i].uniformAllocation = VK_NULL_HANDLE;
        outWindow->frames[i].uniformMapped     = NULL;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    
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
    glfwSetFramebufferSizeCallback(outWindow->handle, framebufferResizeCallback);
    
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
    result = create_depth_image(ctx, outWindow);
    if (result.status != VULKAN_SUCCESS) {
        LOG_ERROR("Depth image creation failed.");
        vkWindowDestroy(ctx, outWindow);
        return result;
    }
    result = create_render_semaphores(ctx, outWindow);
    if (result.status != VULKAN_SUCCESS) {
        LOG_ERROR("Render semaphore creation failed.");
        vkWindowDestroy(ctx, outWindow);
        return result;
    }
    result = create_frame_data(ctx, outWindow);
    if (result.status != VULKAN_SUCCESS) {
        LOG_ERROR("Vulkan frame data creation failed.");
        vkWindowDestroy(ctx, outWindow);
        return result;
    }
    result = create_image_data(ctx, outWindow);
    if (result.status != VULKAN_SUCCESS) {
        LOG_ERROR("Vulkan image data creation failed.");
        vkWindowDestroy(ctx, outWindow);
        return result;
    }
    result = create_descriptor_pool(ctx, outWindow);
    if (result.status != VULKAN_SUCCESS) {
        LOG_ERROR("descriptor pool creation failed.");
        vkWindowDestroy(ctx, outWindow);
        return result;
    }
    result = create_frame_descriptors(ctx, outWindow);
    if (result.status != VULKAN_SUCCESS) {
        LOG_ERROR("descriptor sets creation failed.");
        vkWindowDestroy(ctx, outWindow);
        return result;
    }
    outWindow->isInitialized = true;
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

    if (window->windowDescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(ctx->logicalDevice, window->windowDescriptorPool, NULL);
        window->windowDescriptorPool = VK_NULL_HANDLE;
    }

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        if (window->frames[i].presentSemaphore != VK_NULL_HANDLE) {
            vkDestroySemaphore(ctx->logicalDevice, window->frames[i].presentSemaphore, NULL);
        }
        if (window->frames[i].renderFence != VK_NULL_HANDLE) {
            vkDestroyFence(ctx->logicalDevice, window->frames[i].renderFence, NULL);
        }
        if (window->frames[i].uniformBuffer != VK_NULL_HANDLE) {
            vmaDestroyBuffer(ctx->allocator, window->frames[i].uniformBuffer, window->frames[i].uniformAllocation);
            window->frames[i].uniformBuffer = VK_NULL_HANDLE;
        }
    }

    if (window->renderSemaphores != NULL) {
        for (uint32_t i = 0; i < window->swapChainImageCount; i++) {
            if (window->renderSemaphores[i] != VK_NULL_HANDLE)
                vkDestroySemaphore(ctx->logicalDevice, window->renderSemaphores[i], NULL);
        }
        free(window->renderSemaphores);
        window->renderSemaphores = NULL;
    }

    if (window->surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(ctx->instance, window->surface, NULL);
        window->surface = VK_NULL_HANDLE;
    }

    if (window->handle != NULL) {
        glfwDestroyWindow(window->handle);
        window->handle = NULL;
    }
    window->isInitialized = false;
}