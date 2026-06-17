#include "vulkan_ctx.h"
#include "logger.h"
#include "window.h"
#include "render_types.h"
#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "texture.h"

const char *validationLayers[] = {
    "VK_LAYER_KHRONOS_validation"};
const uint32_t validationLayersCount = sizeof(validationLayers) / sizeof(validationLayers[0]);
#ifdef __APPLE__
const char *deviceExtensions[] = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    "VK_KHR_portability_subset" 
};
#else
const char *deviceExtensions[] = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME
};
#endif
const uint32_t deviceExtensionsCount = sizeof(deviceExtensions) / sizeof(deviceExtensions[0]);


static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
    void *pUserData)
{
    fprintf(stderr, "[Validation Layer]: %s\n\n", pCallbackData->pMessage);
    return VK_FALSE;
}

static int check_validation_layer_support(void) {
    uint32_t layerCount;
    vkEnumerateInstanceLayerProperties(&layerCount, NULL);
    VkLayerProperties *availableLayers = malloc(layerCount * sizeof(VkLayerProperties));
    if (availableLayers == NULL)
        return 0;
    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers);
    for (uint32_t i = 0; i < validationLayersCount; i++) {
        int layerFound = 0;
        for (uint32_t j = 0; j < layerCount; j++) {
            if (strcmp(validationLayers[i], availableLayers[j].layerName) == 0) {
                layerFound = 1;
                break;
            }
        }
        if (!layerFound) {
            free(availableLayers);
            return 0;
        }
    }
    free(availableLayers);
    return 1;
}

static const char **get_required_extensions(VkContext* ctx, bool validation, uint32_t *out_extension_count)
{
    uint32_t extension_count = 0;
    if (ctx->presentationEnabled) {
        uint32_t glfw_extension_count = 0;
        const char **glfw_extensions = glfwGetRequiredInstanceExtensions(&glfw_extension_count);
        extension_count += glfw_extension_count;
    }

    if (validation) {
        extension_count++;
    }
#ifdef __APPLE__
    extension_count++;
#endif

    *out_extension_count = extension_count;

    const char **extensions = malloc((*out_extension_count) * sizeof(*extensions));
    if (extensions == NULL) return NULL;

    uint32_t idx = 0;
    if (ctx->presentationEnabled) {
        uint32_t glfw_extension_count = 0;
        const char **glfw_extensions = glfwGetRequiredInstanceExtensions(&glfw_extension_count);
        
        for (uint32_t i = 0; i < glfw_extension_count; i++) {
            extensions[idx++] = glfw_extensions[i];
        }
    }

    if (validation) {
        extensions[idx++] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
    }

#ifdef __APPLE__
    extensions[idx++] = VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME;
#endif

    return extensions;
}

static int check_extension_support(
    const char **required_extensions,
    const uint32_t required_extension_count,
    const VkExtensionProperties *vulkan_extensions,
    const uint32_t vulkan_extension_count)
{
    for (uint32_t i = 0; i < required_extension_count; i++)
    {
        int extension_found = 0;
        for (uint32_t j = 0; j < vulkan_extension_count; j++)
        {
            if (strcmp(required_extensions[i], vulkan_extensions[j].extensionName) == 0)
            {
                extension_found = 1;
                break;
            }
        }
        if (!extension_found)
            return 0;
    }
    return 1;
}

static VulkanResult create_instance(VkContextCreateInfo* createInfo, VkContext* ctx) {
    bool enableValidation = createInfo->validationLayers;
    if (enableValidation && !check_validation_layer_support()) {
        LOG_WARN("Vulkan validation layers requested but not available. Continuing without them.");
        enableValidation = false; 
    }
    createInfo->validationLayers = enableValidation;
    
    VkApplicationInfo application_info = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pNext = NULL,
        .pApplicationName = createInfo->appName,
        .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
        .pEngineName = "No Engine",
        .engineVersion = VK_MAKE_VERSION(1, 0, 0),
        .apiVersion = VK_API_VERSION_1_3,
    };
    
    uint32_t required_extensions_count = 0;
    const char **required_extensions = get_required_extensions(ctx, enableValidation, &required_extensions_count);
    if (required_extensions == NULL) {
        LOG_ERROR("Out of memory while allocating required instance extensions list.");
        return (VulkanResult){.status = VULKAN_ERROR_OUT_OF_MEMORY, .vk_result = VK_SUCCESS};
    }
    
    uint32_t extensions_count = 0;
    VkResult enumerate_result = vkEnumerateInstanceExtensionProperties(NULL, &extensions_count, NULL);
    if (enumerate_result != VK_SUCCESS || extensions_count == 0) {
        LOG_ERROR("Failed to enumerate instance extension properties. VkResult: %i", enumerate_result);
        free(required_extensions);
        return (VulkanResult){.status = VULKAN_ERROR_EXTENSION_FETCH_FAILED, .vk_result = enumerate_result};
    }

    VkExtensionProperties *extensions = malloc(extensions_count * sizeof(VkExtensionProperties));
    if (extensions == NULL) {
        LOG_ERROR("Out of memory while allocating instance extension properties.");
        free(required_extensions);
        return (VulkanResult){.status = VULKAN_ERROR_OUT_OF_MEMORY, .vk_result = VK_SUCCESS};
    }

    enumerate_result = vkEnumerateInstanceExtensionProperties(NULL, &extensions_count, extensions);
    if (enumerate_result != VK_SUCCESS) {
        LOG_ERROR("Failed to enumerate instance extension properties. VkResult: %i", enumerate_result);
        free(extensions);
        free(required_extensions);
        return (VulkanResult){.status = VULKAN_ERROR_EXTENSION_FETCH_FAILED, .vk_result = enumerate_result};
    }

    if (!check_extension_support(required_extensions, required_extensions_count, extensions, extensions_count))
    {
        LOG_ERROR("One or more required Vulkan instance extensions are not supported on this platform.");
        free(extensions);
        free(required_extensions);
        return (VulkanResult){.status = VULKAN_STATUS_EXTENSIONS_UNSUPPORTED, .vk_result = VK_SUCCESS};
    }

    VkInstanceCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext = NULL,
        .pApplicationInfo = &application_info,
        .enabledExtensionCount = required_extensions_count,
        .ppEnabledExtensionNames = required_extensions,
        .enabledLayerCount = 0,
        .ppEnabledLayerNames = NULL,
        .flags = 0,
    };

    #ifdef __APPLE__
        create_info.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    #endif

    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .pNext = NULL,
        .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
        .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
        .pfnUserCallback = debugCallback,
        .pUserData = NULL
    };

    if (enableValidation) {
        create_info.enabledLayerCount = validationLayersCount;
        create_info.ppEnabledLayerNames = validationLayers;
        create_info.pNext = &debugCreateInfo;
    }
    const VkResult create_result = vkCreateInstance(&create_info, NULL, &ctx->instance);

    free(extensions);
    free(required_extensions);

    if (create_result != VK_SUCCESS)
    {
        LOG_ERROR("vkCreateInstance failed. VkResult: %i", create_result);
        return (VulkanResult){.status = VULKAN_ERROR_INSTANCE_CREATION_FAILED, .vk_result = create_result};
    }

    LOG_INFO("Vulkan instance created (validation layers: %s).", enableValidation ? "enabled" : "disabled");
    return (VulkanResult){.status = VULKAN_SUCCESS, .vk_result = VK_SUCCESS};
}

static VkResult create_debug_utils_messenger_ext(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkDebugUtilsMessengerEXT *pDebugMessenger)
{
    const PFN_vkCreateDebugUtilsMessengerEXT func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
    if (func != NULL)
    {
        return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
    }
    return VK_ERROR_EXTENSION_NOT_PRESENT;
}

static void destroy_debug_utils_messenger_ext(VkInstance instance, VkDebugUtilsMessengerEXT debugMessenger, const VkAllocationCallbacks *pAllocator)
{
    const PFN_vkDestroyDebugUtilsMessengerEXT func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
    if (func != NULL)
    {
        func(instance, debugMessenger, pAllocator);
    }
}

static VulkanResult setup_debug_messenger(VkContext* ctx, bool validation) {
    if (!validation)
        return (VulkanResult){.status = VULKAN_SUCCESS, .vk_result = VK_SUCCESS};
    VkDebugUtilsMessengerCreateInfoEXT createInfo = {
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .pNext = NULL,
        .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
        .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
        .pfnUserCallback = debugCallback,
        .pUserData = NULL};

    VkResult result = create_debug_utils_messenger_ext(ctx->instance, &createInfo, NULL, &ctx->debugMessenger);
    if (result != VK_SUCCESS)
    {
        LOG_ERROR("Failed to create debug utils messenger. VkResult: %i", result);
        return (VulkanResult){.status = VULKAN_ERROR_INSTANCE_CREATION_FAILED, .vk_result = result};
    }

    return (VulkanResult){.status = VULKAN_SUCCESS, .vk_result = VK_SUCCESS};
}

static int check_device_extension_support(VkPhysicalDevice device)
{
    uint32_t extensionCount;
    vkEnumerateDeviceExtensionProperties(device, NULL, &extensionCount, NULL);

    VkExtensionProperties *availableExtensions = malloc(extensionCount * sizeof(VkExtensionProperties));
    vkEnumerateDeviceExtensionProperties(device, NULL, &extensionCount, availableExtensions);

    int allExtensionsSupported = 1;

    for (uint32_t i = 0; i < deviceExtensionsCount; i++)
    {
        int extensionFound = 0;
        for (uint32_t j = 0; j < extensionCount; j++)
        {
            if (strcmp(deviceExtensions[i], availableExtensions[j].extensionName) == 0)
            {
                extensionFound = 1;
                break;
            }
        }
        if (!extensionFound)
        {
            allExtensionsSupported = 0;
            break;
        }
    }

    free(availableExtensions);
    return allExtensionsSupported;
}

static uint32_t rate_device_suitability(VkPhysicalDevice device, VkSurfaceKHR surface)
{
    VkPhysicalDeviceProperties deviceProperties;
    vkGetPhysicalDeviceProperties(device, &deviceProperties);
    if (deviceProperties.apiVersion < VK_API_VERSION_1_3)
    {
        return 0;
    }
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, NULL);
    VkQueueFamilyProperties *queueFamilies = malloc(queueFamilyCount * sizeof(VkQueueFamilyProperties));
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies);
    int hasRequiredQueues = 0;
    if (surface != VK_NULL_HANDLE)
    {
        for (uint32_t i = 0; i < queueFamilyCount; i++)
        {
            if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
            {
                hasRequiredQueues = 1;
                break;
            }
        }
    }
    else
    {
        for (uint32_t i = 0; i < queueFamilyCount; i++)
        {
            if ((queueFamilies[i].queueFlags & VK_QUEUE_COMPUTE_BIT) || (queueFamilies[i].queueFlags & VK_QUEUE_TRANSFER_BIT))
            {
                hasRequiredQueues = 1;
                break;
            }
        }
    }
    free(queueFamilies);
    if (!hasRequiredQueues)
    {
        return 0;
    }
    if (surface != VK_NULL_HANDLE)
    {
        if (!check_device_extension_support(device))
        {
            return 0;
        }

        uint32_t formatCount = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, NULL);
        uint32_t presentModeCount = 0;
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, NULL);
        if (formatCount == 0 || presentModeCount == 0)
        {
            return 0;
        }
    }
    VkPhysicalDeviceExtendedDynamicStateFeaturesEXT extDynamicState = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT,
        .pNext = NULL};

    VkPhysicalDeviceVulkan13Features vk13Features = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .pNext = &extDynamicState};

    VkPhysicalDeviceFeatures2 features2 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = &vk13Features};

    vkGetPhysicalDeviceFeatures2(device, &features2);
    if (!vk13Features.dynamicRendering || !extDynamicState.extendedDynamicState ||
        !features2.features.samplerAnisotropy)
    {
        return 0;
    }
    uint32_t score = 1;
    if (deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
    {
        score += 1000;
    }
    score += deviceProperties.limits.maxImageDimension2D;

    return score;
}

VkSampleCountFlagBits vkContextGetMaxUsableSampleCount(VkPhysicalDevice physicalDevice) {
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(physicalDevice, &props);

    VkSampleCountFlags counts =
        props.limits.framebufferColorSampleCounts &
        props.limits.framebufferDepthSampleCounts;

    if (counts & VK_SAMPLE_COUNT_8_BIT)  return VK_SAMPLE_COUNT_8_BIT;
    if (counts & VK_SAMPLE_COUNT_4_BIT)  return VK_SAMPLE_COUNT_4_BIT;
    if (counts & VK_SAMPLE_COUNT_2_BIT)  return VK_SAMPLE_COUNT_2_BIT;
    return VK_SAMPLE_COUNT_1_BIT;
}

static VulkanResult pick_physical_device(VkContext* ctx, VkSurfaceKHR surface) {
        if (ctx->physicalDevice != VK_NULL_HANDLE) {
        return (VulkanResult){.status = VULKAN_SUCCESS, .vk_result = VK_SUCCESS};
    }

    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(ctx->instance, &deviceCount, NULL);
    if (deviceCount == 0) {
        LOG_ERROR("Failed to find any GPUs with Vulkan support.");
        return (VulkanResult){.status = VULKAN_ERROR_NO_SUITABLE_GPU, .vk_result = VK_ERROR_UNKNOWN};
    }

    VkPhysicalDevice* devices = malloc(deviceCount * sizeof(VkPhysicalDevice));
    vkEnumeratePhysicalDevices(ctx->instance, &deviceCount, devices);

    VkPhysicalDevice chosenDevice = VK_NULL_HANDLE;
    uint32_t highestScore = 0;

    for (uint32_t i = 0; i < deviceCount; i++) {
        uint32_t score = rate_device_suitability(devices[i], surface);
        if (score > highestScore) {
            highestScore = score;
            chosenDevice = devices[i];
        }
    }
    free(devices);

    if (chosenDevice == VK_NULL_HANDLE || highestScore == 0) {
        LOG_ERROR("Failed to find a suitable GPU.");
        return (VulkanResult){.status = VULKAN_ERROR_NO_SUITABLE_GPU, .vk_result = VK_ERROR_UNKNOWN};
    }

    ctx->physicalDevice = chosenDevice;

    VkPhysicalDeviceProperties deviceProperties;
    vkGetPhysicalDeviceProperties(ctx->physicalDevice, &deviceProperties);
    LOG_INFO("Selected Hardware: %s", deviceProperties.deviceName);

    VkSampleCountFlagBits requested = ctx->msaaSamples;
    VkSampleCountFlagBits maxSupported = vkContextGetMaxUsableSampleCount(ctx->physicalDevice);

    if (requested == VK_SAMPLE_COUNT_1_BIT) {
        LOG_INFO("MSAA disabled.");
    } else {
        VkSampleCountFlagBits counts = deviceProperties.limits.framebufferColorSampleCounts
                                     & deviceProperties.limits.framebufferDepthSampleCounts;
        VkSampleCountFlagBits resolved = VK_SAMPLE_COUNT_1_BIT;
        VkSampleCountFlagBits candidates[] = {
            VK_SAMPLE_COUNT_64_BIT, VK_SAMPLE_COUNT_32_BIT,
            VK_SAMPLE_COUNT_16_BIT, VK_SAMPLE_COUNT_8_BIT,
            VK_SAMPLE_COUNT_4_BIT,  VK_SAMPLE_COUNT_2_BIT,
        };
        for (uint32_t i = 0; i < 6; i++) {
            if (candidates[i] > requested) continue; 
            if (counts & candidates[i]) { resolved = candidates[i]; break; }
        }
        ctx->msaaSamples = resolved;
        if (resolved != requested) {
            LOG_WARN("Requested MSAA x%u is not supported; falling back to x%u.",
                     (uint32_t)requested, (uint32_t)resolved);
        } else {
            LOG_INFO("MSAA x%u enabled.", (uint32_t)resolved);
        }
        (void)maxSupported;
    }

    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(ctx->physicalDevice, &queueFamilyCount, NULL);
    VkQueueFamilyProperties* queueFamilies = malloc(queueFamilyCount * sizeof(VkQueueFamilyProperties));
    vkGetPhysicalDeviceQueueFamilyProperties(ctx->physicalDevice, &queueFamilyCount, queueFamilies);

    ctx->queues.graphicsFamilyIndex = UINT32_MAX;
    ctx->queues.computeFamilyIndex  = UINT32_MAX;
    ctx->queues.transferFamilyIndex = UINT32_MAX;

    for (uint32_t i = 0; i < queueFamilyCount; i++) {
        if (surface != VK_NULL_HANDLE) {
            if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                VkBool32 presentSupport = VK_FALSE;
                vkGetPhysicalDeviceSurfaceSupportKHR(ctx->physicalDevice, i, surface, &presentSupport);
                if (presentSupport && ctx->queues.graphicsFamilyIndex == UINT32_MAX) {
                    ctx->queues.graphicsFamilyIndex = i;
                }
            }
        }
        if (queueFamilies[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
            if (ctx->queues.computeFamilyIndex == UINT32_MAX) ctx->queues.computeFamilyIndex = i;
        }
        if (queueFamilies[i].queueFlags & VK_QUEUE_TRANSFER_BIT) {
            if (ctx->queues.transferFamilyIndex == UINT32_MAX) ctx->queues.transferFamilyIndex = i;
        }
    }
    free(queueFamilies);
    return (VulkanResult){.status = VULKAN_SUCCESS, .vk_result = VK_SUCCESS};
}

static VulkanResult create_logical_device(VkContext* ctx) {
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(ctx->physicalDevice, &queueFamilyCount, NULL);

    VkQueueFamilyProperties *queueFamilies = malloc(queueFamilyCount * sizeof(VkQueueFamilyProperties));
    if (queueFamilies == NULL)
    {
        LOG_ERROR("Out of memory while allocating queue family properties.");
        return (VulkanResult){.status = VULKAN_ERROR_OUT_OF_MEMORY, .vk_result = VK_SUCCESS};
    }
    vkGetPhysicalDeviceQueueFamilyProperties(ctx->physicalDevice, &queueFamilyCount, queueFamilies);

    uint32_t graphicsIndex = UINT32_MAX;
    uint32_t computeIndex = UINT32_MAX;
    uint32_t transferIndex = UINT32_MAX;

    for (uint32_t i = 0; i < queueFamilyCount; i++)
    {
        if ((queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && graphicsIndex == UINT32_MAX)
        {
            graphicsIndex = i;
        }
        if ((queueFamilies[i].queueFlags & VK_QUEUE_COMPUTE_BIT) && !(queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT))
        {
            computeIndex = i;
        }
        if ((queueFamilies[i].queueFlags & VK_QUEUE_TRANSFER_BIT) &&
            !(queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) &&
            !(queueFamilies[i].queueFlags & VK_QUEUE_COMPUTE_BIT))
        {
            transferIndex = i;
        }
    }

    if (computeIndex == UINT32_MAX)
    {
        for (uint32_t i = 0; i < queueFamilyCount; i++)
        {
            if (queueFamilies[i].queueFlags & VK_QUEUE_COMPUTE_BIT)
            {
                computeIndex = i;
                break;
            }
        }
    }
    if (transferIndex == UINT32_MAX)
    {
        for (uint32_t i = 0; i < queueFamilyCount; i++)
        {
            if (queueFamilies[i].queueFlags & VK_QUEUE_TRANSFER_BIT)
            {
                transferIndex = i;
                break;
            }
        }
    }

    free(queueFamilies);

    if (graphicsIndex == UINT32_MAX || computeIndex == UINT32_MAX || transferIndex == UINT32_MAX)
    {
        LOG_ERROR("Failed to find required queue families. Graphics: %s, Compute: %s, Transfer: %s.",
            graphicsIndex == UINT32_MAX ? "missing" : "found",
            computeIndex  == UINT32_MAX ? "missing" : "found",
            transferIndex == UINT32_MAX ? "missing" : "found");
        return (VulkanResult){.status = VULKAN_ERROR_QUEUE_FETCH_FAILED, .vk_result = VK_SUCCESS};
    }

    uint32_t indicesToCheck[3] = {graphicsIndex, computeIndex, transferIndex};
    uint32_t uniqueIndices[3];
    uint32_t uniqueCount = 0;

    for (int i = 0; i < 3; i++)
    {
        int alreadyExists = 0;
        for (uint32_t j = 0; j < uniqueCount; j++)
        {
            if (uniqueIndices[j] == indicesToCheck[i])
            {
                alreadyExists = 1;
                break;
            }
        }
        if (!alreadyExists)
        {
            uniqueIndices[uniqueCount++] = indicesToCheck[i];
        }
    }

    VkDeviceQueueCreateInfo queueCreateInfos[3];
    float queuePriority = 1.0f;

    for (uint32_t i = 0; i < uniqueCount; i++)
    {
        queueCreateInfos[i] = (VkDeviceQueueCreateInfo){
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .pNext = NULL,
            .flags = 0,
            .queueFamilyIndex = uniqueIndices[i],
            .queueCount = 1,
            .pQueuePriorities = &queuePriority};
    }

    VkPhysicalDeviceVulkan11Features vk11Features = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
        .pNext = NULL,
        .shaderDrawParameters = VK_TRUE
    };

    VkPhysicalDeviceVulkan12Features vk12Features = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
        .pNext = &vk11Features,
        .drawIndirectCount = VK_TRUE,
        .descriptorBindingPartiallyBound = VK_TRUE,
        .runtimeDescriptorArray = VK_TRUE,
        .descriptorBindingSampledImageUpdateAfterBind = VK_TRUE
    };

    VkPhysicalDeviceExtendedDynamicStateFeaturesEXT extDynamicState = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT,
        .pNext = &vk12Features, 
        .extendedDynamicState = VK_TRUE
    };

    VkPhysicalDeviceVulkan13Features vk13Features = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .pNext = &extDynamicState,
        .dynamicRendering = VK_TRUE,
        .synchronization2 = VK_TRUE
    };

    VkPhysicalDeviceFeatures2 features2 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = &vk13Features,
        .features.samplerAnisotropy = VK_TRUE,
        .features.fillModeNonSolid = VK_TRUE,
        .features.sampleRateShading = (ctx->msaaSamples > VK_SAMPLE_COUNT_1_BIT) ? VK_TRUE : VK_FALSE,
    };

    VkDeviceCreateInfo createInfo = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &features2,
        .flags = 0,
        .queueCreateInfoCount = uniqueCount,
        .pQueueCreateInfos = queueCreateInfos, 
        .enabledLayerCount = 0,
        .ppEnabledLayerNames = NULL,
        .enabledExtensionCount = deviceExtensionsCount,
        .ppEnabledExtensionNames = deviceExtensions,
        .pEnabledFeatures = NULL
    };

    VkResult result = vkCreateDevice(ctx->physicalDevice, &createInfo, NULL, &ctx->logicalDevice);
    if (result != VK_SUCCESS)
    {
        LOG_ERROR("vkCreateDevice failed. VkResult: %i", result);
        return (VulkanResult){.status = VULKAN_ERROR_LOGICAL_DEVICE_CREATION_FAILED, .vk_result = result};
    }

    vkGetDeviceQueue(ctx->logicalDevice, graphicsIndex, 0, &ctx->queues.graphics);
    vkGetDeviceQueue(ctx->logicalDevice, computeIndex, 0, &ctx->queues.compute);
    vkGetDeviceQueue(ctx->logicalDevice, transferIndex, 0, &ctx->queues.transfer);

    ctx->queues.graphicsFamilyIndex = graphicsIndex;
    ctx->queues.computeFamilyIndex = computeIndex;
    ctx->queues.transferFamilyIndex = transferIndex;

    LOG_INFO("Logical device created. Queue families — Graphics: %u, Compute: %u, Transfer: %u.",
        graphicsIndex, computeIndex, transferIndex);

    VmaAllocatorCreateInfo allocatorInfo = {
        .physicalDevice = ctx->physicalDevice,
        .device = ctx->logicalDevice,
        .instance = ctx->instance,
        .vulkanApiVersion = VK_API_VERSION_1_3
    };
    result = vmaCreateAllocator(&allocatorInfo, &ctx->allocator);
    if (result != VK_SUCCESS)
    {
        LOG_ERROR("vmaCreateAllocator failed. VkResult: %i", result);
        return (VulkanResult){.status = VULKAN_ERROR_VMA_ALLOCATOR_CREATION_FAILED, .vk_result = result};
    }
    return (VulkanResult){.status = VULKAN_SUCCESS, .vk_result = VK_SUCCESS};
}

static VulkanResult create_transfer_pool(VkContext* ctx) {
    VkCommandPoolCreateInfo poolInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .pNext = NULL,
        .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
        .queueFamilyIndex = ctx->queues.transferFamilyIndex
    };
    VkResult poolResult = vkCreateCommandPool(ctx->logicalDevice, &poolInfo, NULL, &ctx->transferPool);
    if (poolResult != VK_SUCCESS) {
        return (VulkanResult){.status = VULKAN_ERROR_COMMAND_POOL_CREATION_FAILED, .vk_result = poolResult};
    }
    return (VulkanResult){.status = VULKAN_SUCCESS, .vk_result = VK_SUCCESS};
}

static VulkanResult create_global_ssbo_ubo(VkContext* ctx) {
    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(ctx->physicalDevice, &properties);

    VkDeviceSize ssboAlignment = properties.limits.minStorageBufferOffsetAlignment;
    VkDeviceSize rawSizePerFrame = sizeof(ObjectSSBO) * MAX_OBJECTS;
    ctx->objectFrameStride = (rawSizePerFrame + ssboAlignment - 1) & ~(ssboAlignment - 1);

    VulkanResult res = vkBufferCreate(
        ctx,
        ctx->objectFrameStride * MAX_FRAMES_IN_FLIGHT,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
        &ctx->objectStorageBuffer
    );
    if (res.status != VULKAN_SUCCESS) {
        LOG_ERROR("Failed to allocate global context SSBO via VulkanBuffer abstraction.");
        return res;
    }
    LOG_INFO("Global Context SSBO created successfully. Stride per frame: %lu bytes. Total: %lu bytes.",
             (unsigned long)ctx->objectFrameStride,
             (unsigned long)(ctx->objectFrameStride * MAX_FRAMES_IN_FLIGHT));

    VkDeviceSize uboAlignment = properties.limits.minUniformBufferOffsetAlignment;
    ctx->uniformFrameStride = (sizeof(GlobalUBO) + uboAlignment - 1) & ~(uboAlignment - 1);

    res = vkBufferCreate(
        ctx,
        ctx->uniformFrameStride * MAX_FRAMES_IN_FLIGHT,
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
        &ctx->uniformBuffer
    );
    if (res.status != VULKAN_SUCCESS) {
        LOG_ERROR("Failed to allocate global context UBO via VulkanBuffer abstraction.");
        return res;
    }
    LOG_INFO("Global Context UBO created successfully. Stride per frame: %lu bytes. Total: %lu bytes.",
             (unsigned long)ctx->uniformFrameStride,
             (unsigned long)(ctx->uniformFrameStride * MAX_FRAMES_IN_FLIGHT));

    return (VulkanResult){.status = VULKAN_SUCCESS, .vk_result = VK_SUCCESS};
}

static VulkanResult create_descriptor_layouts(VkContext* ctx) {
    VkDescriptorSetLayoutBinding bindings[6] = {
        {
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        },
        {
            .binding = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        },
        {
            .binding = 2, 
            .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            .descriptorCount = MAX_TEXTURES / 2,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        },
        {
            .binding = 3, 
            .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            .descriptorCount = MAX_TEXTURES / 2,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        },
        {
            .binding = 4, 
            .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
            .pImmutableSamplers = NULL
        },
        {
            .binding = 5, 
            .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
            .pImmutableSamplers = NULL
        }
    };

    VkDescriptorBindingFlags bindingFlags[6] = {
        0, // UBO
        0, // SSBO
        VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT, 
        VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT,
        0,  
        0
    };

    VkDescriptorSetLayoutBindingFlagsCreateInfo extInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
        .bindingCount = 6,
        .pBindingFlags = bindingFlags
    };

    VkDescriptorSetLayoutCreateInfo layoutInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .pNext = &extInfo,
        .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT,
        .bindingCount = 6,
        .pBindings = bindings
    };

    VkResult result = vkCreateDescriptorSetLayout(ctx->logicalDevice, &layoutInfo, NULL, &ctx->globalSetLayout);
    if (result != VK_SUCCESS) {
        LOG_ERROR("vkCreateDescriptorSetLayout failed for global set. VkResult: %i", result);
        return (VulkanResult){.status = VULKAN_ERROR_DESCRIPTOR_SET_LAYOUT_CREATION_FAILED, .vk_result = result};
    }
    return (VulkanResult){.status = VULKAN_SUCCESS, .vk_result = VK_SUCCESS};
}

static VulkanResult create_descriptor_pool(VkContext* ctx) {
    VkDescriptorPoolSize poolSizes[6] = {
        { .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount = MAX_FRAMES_IN_FLIGHT },
        { .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = MAX_FRAMES_IN_FLIGHT },
        { .type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,  .descriptorCount = MAX_TEXTURES / 2 * MAX_FRAMES_IN_FLIGHT },
        { .type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,  .descriptorCount = MAX_TEXTURES / 2 * MAX_FRAMES_IN_FLIGHT },
        { .type = VK_DESCRIPTOR_TYPE_SAMPLER,        .descriptorCount = MAX_FRAMES_IN_FLIGHT },
        { .type = VK_DESCRIPTOR_TYPE_SAMPLER,        .descriptorCount = MAX_FRAMES_IN_FLIGHT },
    };
    VkDescriptorPoolCreateInfo poolInfo = {
        .sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .flags         = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
        .maxSets       = MAX_FRAMES_IN_FLIGHT,
        .poolSizeCount = 6,
        .pPoolSizes    = poolSizes,
    };
    VkResult result = vkCreateDescriptorPool(ctx->logicalDevice, &poolInfo, NULL, &ctx->globalDescriptorPool);
    if (result != VK_SUCCESS) {
        LOG_ERROR("vkCreateDescriptorPool failed. VkResult: %i", result);
        return (VulkanResult){.status = VULKAN_ERROR_DESCRIPTOR_POOL_CREATION_FAILED, .vk_result = result};
    }
    return (VulkanResult){.status = VULKAN_SUCCESS, .vk_result = VK_SUCCESS};
}

static VulkanResult create_frame_descriptors(VkContext* ctx) {
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkDescriptorSetAllocateInfo allocInfo = {
            .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool     = ctx->globalDescriptorPool,
            .descriptorSetCount = 1,
            .pSetLayouts        = &ctx->globalSetLayout,
        };
        VkResult result = vkAllocateDescriptorSets(ctx->logicalDevice, &allocInfo, &ctx->globalDescriptorSets[i]);
        if (result != VK_SUCCESS) {
            LOG_ERROR("vkAllocateDescriptorSets failed for frame %u. VkResult: %i", i, result);
            return (VulkanResult){.status = VULKAN_ERROR_DESCRIPTOR_SET_ALLOCATION_FAILED, .vk_result = result};
        }

        // binding 0: UBO
        VkDescriptorBufferInfo uboInfo = {
            .buffer = ctx->uniformBuffer.buffer,
            .offset = i * ctx->uniformFrameStride,
            .range  = sizeof(GlobalUBO),
        };

        // binding 1: SSBO
        VkDescriptorBufferInfo ssboInfo = {
            .buffer = ctx->objectStorageBuffer.buffer,
            .offset = i * ctx->objectFrameStride,
            .range  = sizeof(ObjectSSBO) * MAX_OBJECTS,
        };

        // binding 3: sampler
        VkDescriptorImageInfo samplerInfo = {
            .sampler     = ctx->linearSampler,
            .imageView   = VK_NULL_HANDLE,
            .imageLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        };

        VkDescriptorImageInfo samplerInfo2 = {
            .sampler     = ctx->nearestSampler,
            .imageView   = VK_NULL_HANDLE,
            .imageLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        };

        VkWriteDescriptorSet writes[4] = {
            {
                .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet          = ctx->globalDescriptorSets[i],
                .dstBinding      = 0,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .pBufferInfo     = &uboInfo,
            },
            {
                .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet          = ctx->globalDescriptorSets[i],
                .dstBinding      = 1,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .pBufferInfo     = &ssboInfo,
            },
            {
                .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet          = ctx->globalDescriptorSets[i],
                .dstBinding      = 4,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType  = VK_DESCRIPTOR_TYPE_SAMPLER,
                .pImageInfo      = &samplerInfo,
            },
            {
                .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet          = ctx->globalDescriptorSets[i],
                .dstBinding      = 5,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType  = VK_DESCRIPTOR_TYPE_SAMPLER,
                .pImageInfo      = &samplerInfo2,
            },
        };
        vkUpdateDescriptorSets(ctx->logicalDevice, 4, writes, 0, NULL);
    }
    return (VulkanResult){.status = VULKAN_SUCCESS, .vk_result = VK_SUCCESS};
}

static VulkanResult create_global_sampler(VkContext* ctx) {
    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(ctx->physicalDevice, &properties);
    float maxAnisotropySupported = properties.limits.maxSamplerAnisotropy;

    VkSamplerCreateInfo samplerInfo = {
        .sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter    = VK_FILTER_LINEAR,
        .minFilter    = VK_FILTER_LINEAR,
        .mipmapMode   = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .anisotropyEnable = VK_TRUE,
        .maxAnisotropy    = maxAnisotropySupported,
        .maxLod           = VK_LOD_CLAMP_NONE,
        .borderColor      = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
    };

    VkResult result = vkCreateSampler(ctx->logicalDevice, &samplerInfo, NULL, &ctx->linearSampler);
    if (result != VK_SUCCESS) {
        LOG_ERROR("vkCreateSampler failed. VkResult: %i", result);
        return (VulkanResult){.status = VULKAN_ERROR_IMAGE_CREATION_FAILED, .vk_result = result};
    }
    VkSamplerCreateInfo nearestSamplerInfo = samplerInfo; 
    nearestSamplerInfo.magFilter = VK_FILTER_NEAREST;
    nearestSamplerInfo.minFilter = VK_FILTER_NEAREST;
    nearestSamplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    result = vkCreateSampler(ctx->logicalDevice, &nearestSamplerInfo, NULL, &ctx->nearestSampler);
    if (result != VK_SUCCESS) {
        LOG_ERROR("vkCreateSampler failed. VkResult: %i", result);
        return (VulkanResult){.status = VULKAN_ERROR_IMAGE_CREATION_FAILED, .vk_result = result};
    }
    return (VulkanResult){.status = VULKAN_SUCCESS, .vk_result = VK_SUCCESS};
}

VulkanResult vkContextInitializeHardware(VkContext* ctx, VkSurfaceKHR surface) {
    VulkanResult res = pick_physical_device(ctx, surface);
    if (res.status != VULKAN_SUCCESS) {
        LOG_ERROR("Physical device selection failed during hardware initialization. Status: %i", res.status);
        return res;
    }
    res = create_logical_device(ctx);
    if (res.status != VULKAN_SUCCESS) {
        LOG_ERROR("Logical device creation failed during hardware initialization. Status: %i", res.status);
        return res;
    }
    res = create_transfer_pool(ctx);
    if (res.status != VULKAN_SUCCESS) {
        LOG_ERROR("Transfer command pool creation failed. Status: %i", res.status);
        return res;
    }
    res = create_global_ssbo_ubo(ctx);
    if (res.status != VULKAN_SUCCESS) {
        LOG_ERROR("Hardware initialization aborted: Global SSBO or UBO creation failed.");
        return res;
    }
    res = create_global_sampler(ctx);
    if (res.status != VULKAN_SUCCESS) {
        LOG_ERROR("Global sampler creation failed.");
        return res;
    }
    res = create_descriptor_layouts(ctx);
    if (res.status != VULKAN_SUCCESS) {
        LOG_ERROR("Hardware initialization aborted: descriptor layouts creation failed.");
        return res;
    }
    res = create_descriptor_pool(ctx);
    if (res.status != VULKAN_SUCCESS) {
        LOG_ERROR("Hardware initialization aborted: descriptor pool creation failed.");
        return res;
    }
    res = create_frame_descriptors(ctx);
    if (res.status != VULKAN_SUCCESS) {
        LOG_ERROR("Hardware initialization aborted: descriptor sets creation failed.");
        return res;
    }
    LOG_INFO("Vulkan hardware initialization complete.");
    return (VulkanResult){.status = VULKAN_SUCCESS, .vk_result = VK_SUCCESS};
}

VulkanResult vkContextCreate(VkContextCreateInfo* createInfo, VkContext* outCtx) {
    outCtx->instance = VK_NULL_HANDLE;
    outCtx->physicalDevice = VK_NULL_HANDLE;
    outCtx->logicalDevice = VK_NULL_HANDLE;
    outCtx->debugMessenger = VK_NULL_HANDLE;
    outCtx->allocator = VK_NULL_HANDLE;
    outCtx->presentationEnabled = createInfo->enablePresentation;
    outCtx->transferPool = VK_NULL_HANDLE;
    outCtx->textureCount = 0;
    outCtx->msaaSamples  = createInfo->msaaSamples != 0
                           ? createInfo->msaaSamples
                           : VK_SAMPLE_COUNT_1_BIT;

    if (createInfo->enablePresentation) {
        if (!glfwInit()) {
            LOG_ERROR("Failed to initialize GLFW during context creation.");
            return (VulkanResult){.status = VULKAN_ERROR_INSTANCE_CREATION_FAILED,
                                  .vk_result = VK_ERROR_INITIALIZATION_FAILED};
        }
    }

    VulkanResult res = create_instance(createInfo, outCtx);
    if (res.status != VULKAN_SUCCESS) {
        LOG_ERROR("Vulkan context creation aborted: instance creation failed.");
        vkContextDestroy(outCtx);
        return res;
    }
        
    res = setup_debug_messenger(outCtx, createInfo->validationLayers);
    if (res.status != VULKAN_SUCCESS) {
        LOG_ERROR("Vulkan context creation aborted: debug messenger setup failed.");
        vkContextDestroy(outCtx);
        return res;
    }
    if (!outCtx->presentationEnabled) {
        res = vkContextInitializeHardware(outCtx, VK_NULL_HANDLE);
        if (res.status != VULKAN_SUCCESS) {
            LOG_ERROR("Vulkan context creation aborted: hardware initialization failed.");
            vkContextDestroy(outCtx);
            return res;
        }
        LOG_INFO("Vulkan Context running fully headless.");
    }
    LOG_INFO("Vulkan context created successfully.");
    return (VulkanResult){.status = VULKAN_SUCCESS, .vk_result = VK_SUCCESS};
}

void vkContextDestroy(VkContext* ctx) {
    if (ctx == NULL) return;
    if (ctx->linearSampler != VK_NULL_HANDLE) {
        vkDestroySampler(ctx->logicalDevice, ctx->linearSampler, NULL);
        ctx->linearSampler = VK_NULL_HANDLE;
    }
    if (ctx->nearestSampler != VK_NULL_HANDLE) {
        vkDestroySampler(ctx->logicalDevice, ctx->nearestSampler, NULL);
        ctx->nearestSampler = VK_NULL_HANDLE;
    }
    vkBufferDestroy(ctx, &ctx->objectStorageBuffer);
    vkBufferDestroy(ctx, &ctx->uniformBuffer);
    if (ctx->transferPool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(ctx->logicalDevice, ctx->transferPool, NULL);
        ctx->transferPool = VK_NULL_HANDLE;
    }
    if (ctx->globalDescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(ctx->logicalDevice, ctx->globalDescriptorPool, NULL);
        ctx->globalDescriptorPool = VK_NULL_HANDLE;
    }
    if (ctx->globalSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(ctx->logicalDevice, ctx->globalSetLayout, NULL);
        ctx->globalSetLayout = VK_NULL_HANDLE;
    }
    if (ctx->allocator != VK_NULL_HANDLE) {
        vmaDestroyAllocator(ctx->allocator);
        ctx->allocator = VK_NULL_HANDLE;
    }
    if (ctx->logicalDevice != VK_NULL_HANDLE) {
        vkDestroyDevice(ctx->logicalDevice, NULL);
        ctx->logicalDevice = VK_NULL_HANDLE;
    }
    if (ctx->debugMessenger != VK_NULL_HANDLE) {
        destroy_debug_utils_messenger_ext(ctx->instance, ctx->debugMessenger, NULL);
        ctx->debugMessenger = VK_NULL_HANDLE;
    }
    if (ctx->instance != VK_NULL_HANDLE) {
        vkDestroyInstance(ctx->instance, NULL);
        ctx->instance = VK_NULL_HANDLE;
    }
    if (ctx->presentationEnabled) {
        glfwTerminate();
    }
}