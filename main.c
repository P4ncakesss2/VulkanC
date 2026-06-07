// TODO FRAMES IN FLIGHT
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <vulkan/vulkan.h>
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include "tinyfiledialogs.h"

#define WIDTH 800
#define HEIGHT 600

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

#ifndef NDEBUG
#define ENABLE_VALIDATION_LAYERS
#endif

typedef struct VulkanQueues
{
    VkQueue graphics;
    VkQueue compute;
    VkQueue transfer;

    uint32_t graphicsFamilyIndex;
    uint32_t computeFamilyIndex;
    uint32_t transferFamilyIndex;
} VulkanQueues;

typedef struct Application
{
    GLFWwindow *window;
    VkInstance instance;
    VkPhysicalDevice physicalDevice;
    VkDevice device;
    VulkanQueues queues;
    VkDebugUtilsMessengerEXT debugMessenger;
    VkSurfaceKHR surface;
    VkSwapchainKHR swapChain;

    uint32_t swapChainImageCount;
    VkImage *swapChainImages;
    VkSurfaceFormatKHR swapChainSurfaceFormat;
    VkExtent2D swapChainExtent;

    uint32_t swapChainImageViewCount;
    VkImageView* swapChainImageViews;

    VkPipelineLayout pipelineLayout;
    VkPipeline graphicsPipeline;

    VkCommandPool commandPool;
    VkCommandBuffer commandBuffer;

    VkSemaphore presentCompleteSemaphore;
    VkSemaphore renderFinishedSemaphore;
    VkFence drawFence;
} Application;

typedef enum VulkanStatus
{
    VULKAN_SUCCESS = 0,
    VULKAN_STATUS_NO_COMPATIBLE_GPU = 1,
    VULKAN_STATUS_EXTENSIONS_UNSUPPORTED = 2,
    VULKAN_STATUS_LAYERS_UNSUPPORTED = 3,
    VULKAN_ERROR_OUT_OF_MEMORY = -1,
    VULKAN_ERROR_INIT_FAILED = -2,
    VULKAN_ERROR_WINDOW_CREATION_FAILED = -3,
    VULKAN_ERROR_INSTANCE_CREATION_FAILED = -4,
    VULKAN_ERROR_EXTENSION_FETCH_FAILED = -5,
    VULKAN_ERROR_SURFACE_CREATION_FAILED = -6,
    VULKAN_ERROR_SWAPCHAIN_CREATION_FAILED = -7,
    VULKAN_ERROR_IMAGE_VIEW_CREATION_FAILED = -8,
    VULKAN_ERROR_FILE_READ_FAILED = -9,
    VULKAN_ERROR_SHADER_MODULE_CREATION_FAILED = -10,
    VULKAN_ERROR_PIPELINE_LAYOUT_CREATION_FAILED = -11,
    VULKAN_ERROR_PIPELINE_CREATION_FAILED = -12,
    VULKAN_ERROR_COMMAND_POOL_CREATION_FAILED = -13,
    VULKAN_ERROR_COMMAND_BUFFER_CREATION_FAILED = -14,
    VULKAN_ERROR_COMMAND_BUFFER_FAILED_BEGIN = -15,
    VULKAN_ERROR_COMMAND_BUFFER_FAILED_END = -16,
    VULKAN_ERROR_FENCE_WAIT_FAILED = -17,
    VULKAN_ERROR_SWAPCHAIN_NEXT_IMAGE_FAILED = -18,
    VULKAN_ERROR_QUEUE_SUBMIT_FAILED = -19,
    VULKAN_ERROR_QUEUE_PRESENT_FAILED = -20,
} VulkanStatus;

typedef struct VulkanResult
{
    VulkanStatus status;
    VkResult vk_result;
} VulkanResult;

char* read_file(const char* filename, size_t* outSize) {
    FILE* file = fopen(filename, "rb");
    if (!file) {
        fprintf(stderr, "Failed to open file: %s\n", filename);
        return NULL;
    }

    fseek(file, 0, SEEK_END);
    long fileSize = ftell(file);
    if (fileSize < 0) {
        fprintf(stderr, "Failed to determine size of file: %s\n", filename);
        fclose(file);
        return NULL;
    }
    
    fseek(file, 0, SEEK_SET);

    char* buffer = (char*)malloc(fileSize);
    if (!buffer) {
        fprintf(stderr, "Failed to allocate memory for file: %s\n", filename);
        fclose(file);
        return NULL;
    }

    size_t bytesRead = fread(buffer, 1, fileSize, file);
    if (bytesRead != (size_t)fileSize) {
        fprintf(stderr, "Failed to read entire file: %s\n", filename);
        free(buffer);
        fclose(file);
        return NULL;
    }

    fclose(file);
    *outSize = (size_t)fileSize;
    return buffer;
}

static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
    void *pUserData)
{
    fprintf(stderr, "[Validation Layer]: %s\n\n", pCallbackData->pMessage);
    return VK_FALSE;
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

static VulkanResult init_window(Application *app)
{
    glfwInit();

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    app->window = glfwCreateWindow(WIDTH, HEIGHT, "Vulkan", NULL, NULL);
    if (app->window == NULL)
        return (VulkanResult){.status = VULKAN_ERROR_WINDOW_CREATION_FAILED, .vk_result = VK_SUCCESS};

    return (VulkanResult){.status = VULKAN_SUCCESS, .vk_result = VK_SUCCESS};
}

static int check_validation_layer_support(void)
{
    uint32_t layerCount;
    vkEnumerateInstanceLayerProperties(&layerCount, NULL);

    VkLayerProperties *availableLayers = malloc(layerCount * sizeof(VkLayerProperties));
    if (availableLayers == NULL)
        return 0;

    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers);

    for (uint32_t i = 0; i < validationLayersCount; i++)
    {
        int layerFound = 0;
        for (uint32_t j = 0; j < layerCount; j++)
        {
            if (strcmp(validationLayers[i], availableLayers[j].layerName) == 0)
            {
                layerFound = 1;
                break;
            }
        }
        if (!layerFound)
        {
            free(availableLayers);
            return 0;
        }
    }

    free(availableLayers);
    return 1;
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

static const char **get_required_extensions(uint32_t *out_extension_count)
{
    uint32_t glfw_extension_count = 0;
    const char **glfw_extensions = glfwGetRequiredInstanceExtensions(&glfw_extension_count);

    uint32_t extra_count = 0;
#ifdef ENABLE_VALIDATION_LAYERS
    extra_count++;
#endif
#ifdef __APPLE__
    extra_count++;
#endif

    *out_extension_count = glfw_extension_count + extra_count;

    const char **extensions = malloc((*out_extension_count) * sizeof(*extensions));
    if (extensions == NULL) return NULL;

    uint32_t idx = 0;
    for (uint32_t i = 0; i < glfw_extension_count; i++) {
        extensions[idx++] = glfw_extensions[i];
    }

#ifdef ENABLE_VALIDATION_LAYERS
    extensions[idx++] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
#endif

#ifdef __APPLE__
    extensions[idx++] = VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME;
#endif

    return extensions;
}

static VulkanResult create_instance(Application *app)
{
    VkApplicationInfo application_info = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pNext = NULL,
        .pApplicationName = "Hello Triangle",
        .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
        .pEngineName = "No Engine",
        .engineVersion = VK_MAKE_VERSION(1, 0, 0),
        .apiVersion = VK_API_VERSION_1_4,
    };

#ifdef ENABLE_VALIDATION_LAYERS
    if (!check_validation_layer_support())
    {
        return (VulkanResult){.status = VULKAN_STATUS_LAYERS_UNSUPPORTED, .vk_result = VK_SUCCESS};
    }
#endif

    uint32_t required_extensions_count = 0;
    const char **required_extensions = get_required_extensions(&required_extensions_count);
    if (required_extensions == NULL)
    {
        return (VulkanResult){.status = VULKAN_ERROR_OUT_OF_MEMORY, .vk_result = VK_SUCCESS};
    }

    uint32_t extensions_count = 0;
    VkResult enumerate_result = vkEnumerateInstanceExtensionProperties(NULL, &extensions_count, NULL);
    if (enumerate_result != VK_SUCCESS || extensions_count == 0)
    {
        free(required_extensions);
        return (VulkanResult){.status = VULKAN_ERROR_EXTENSION_FETCH_FAILED, .vk_result = enumerate_result};
    }

    VkExtensionProperties *extensions = malloc(extensions_count * sizeof(VkExtensionProperties));
    if (extensions == NULL)
    {
        free(required_extensions);
        return (VulkanResult){.status = VULKAN_ERROR_OUT_OF_MEMORY, .vk_result = VK_SUCCESS};
    }

    enumerate_result = vkEnumerateInstanceExtensionProperties(NULL, &extensions_count, extensions);
    if (enumerate_result != VK_SUCCESS)
    {
        free(extensions);
        free(required_extensions);
        return (VulkanResult){.status = VULKAN_ERROR_EXTENSION_FETCH_FAILED, .vk_result = enumerate_result};
    }

    if (!check_extension_support(required_extensions, required_extensions_count, extensions, extensions_count))
    {
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
        .pUserData = NULL};

#ifdef ENABLE_VALIDATION_LAYERS
    create_info.enabledLayerCount = validationLayersCount;
    create_info.ppEnabledLayerNames = validationLayers;
    create_info.pNext = &debugCreateInfo;
#endif

    const VkResult create_result = vkCreateInstance(&create_info, NULL, &app->instance);

    free(extensions);
    free(required_extensions);

    if (create_result != VK_SUCCESS)
    {
        return (VulkanResult){.status = VULKAN_ERROR_INSTANCE_CREATION_FAILED, .vk_result = create_result};
    }

    return (VulkanResult){.status = VULKAN_SUCCESS, .vk_result = VK_SUCCESS};
}

static VulkanResult setup_debug_messenger(Application *app)
{
#ifndef ENABLE_VALIDATION_LAYERS
    return (VulkanResult){.status = VULKAN_SUCCESS, .vk_result = VK_SUCCESS};
#else
    VkDebugUtilsMessengerCreateInfoEXT createInfo = {
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .pNext = NULL,
        .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
        .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
        .pfnUserCallback = debugCallback,
        .pUserData = NULL};

    VkResult result = create_debug_utils_messenger_ext(app->instance, &createInfo, NULL, &app->debugMessenger);
    if (result != VK_SUCCESS)
    {
        return (VulkanResult){.status = VULKAN_ERROR_INSTANCE_CREATION_FAILED, .vk_result = result};
    }

    return (VulkanResult){.status = VULKAN_SUCCESS, .vk_result = VK_SUCCESS};
#endif
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

    int supportsGraphics = 0;
    for (uint32_t i = 0; i < queueFamilyCount; i++)
    {
        if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
        {
            supportsGraphics = 1;
            break;
        }
    }
    free(queueFamilies);

    if (!supportsGraphics)
    {
        return 0;
    }

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

    if (!vk13Features.dynamicRendering || !extDynamicState.extendedDynamicState || !features2.features.samplerAnisotropy)
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

static VulkanResult pick_physical_device(Application *app)
{
    uint32_t device_count = 0;
    VkResult result = vkEnumeratePhysicalDevices(app->instance, &device_count, NULL);
    if (result != VK_SUCCESS)
    {
        return (VulkanResult){.status = VULKAN_ERROR_INIT_FAILED, .vk_result = result};
    }

    if (device_count == 0)
    {
        return (VulkanResult){.status = VULKAN_STATUS_NO_COMPATIBLE_GPU, .vk_result = VK_SUCCESS};
    }

    VkPhysicalDevice *physical_devices = malloc(device_count * sizeof(VkPhysicalDevice));
    if (physical_devices == NULL)
    {
        return (VulkanResult){.status = VULKAN_ERROR_OUT_OF_MEMORY, .vk_result = VK_SUCCESS};
    }

    result = vkEnumeratePhysicalDevices(app->instance, &device_count, physical_devices);
    if (result != VK_SUCCESS)
    {
        free(physical_devices);
        return (VulkanResult){.status = VULKAN_ERROR_INIT_FAILED, .vk_result = result};
    }

    uint32_t highest_score = 0;

    for (uint32_t i = 0; i < device_count; i++)
    {
        uint32_t score = rate_device_suitability(physical_devices[i], app->surface);
        if (score > highest_score)
        {
            highest_score = score;
            app->physicalDevice = physical_devices[i];
        }
    }

    free(physical_devices);

    if (app->physicalDevice == VK_NULL_HANDLE)
    {
        return (VulkanResult){.status = VULKAN_STATUS_NO_COMPATIBLE_GPU, .vk_result = VK_SUCCESS};
    }

    VkPhysicalDeviceProperties deviceProperties;
    vkGetPhysicalDeviceProperties(app->physicalDevice, &deviceProperties);
    printf("Selected Physical Device: %s (Score: %u)\n", deviceProperties.deviceName, highest_score);

    return (VulkanResult){.status = VULKAN_SUCCESS, .vk_result = VK_SUCCESS};
}

static VulkanResult create_logical_device(Application *app)
{
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(app->physicalDevice, &queueFamilyCount, NULL);

    VkQueueFamilyProperties *queueFamilies = malloc(queueFamilyCount * sizeof(VkQueueFamilyProperties));
    if (queueFamilies == NULL)
    {
        return (VulkanResult){.status = VULKAN_ERROR_OUT_OF_MEMORY, .vk_result = VK_SUCCESS};
    }
    vkGetPhysicalDeviceQueueFamilyProperties(app->physicalDevice, &queueFamilyCount, queueFamilies);

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
        return (VulkanResult){.status = VULKAN_ERROR_INIT_FAILED, .vk_result = VK_SUCCESS};
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

    VkPhysicalDeviceExtendedDynamicStateFeaturesEXT extDynamicState = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT,
        .pNext = &vk11Features,
        .extendedDynamicState = VK_TRUE};

    VkPhysicalDeviceVulkan13Features vk13Features = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .pNext = &extDynamicState,
        .dynamicRendering = VK_TRUE,
        .synchronization2 = VK_TRUE
    };

    VkPhysicalDeviceFeatures2 features2 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = &vk13Features,
        .features.samplerAnisotropy = VK_TRUE};

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
        .pEnabledFeatures = NULL};

    VkResult result = vkCreateDevice(app->physicalDevice, &createInfo, NULL, &app->device);
    if (result != VK_SUCCESS)
    {
        return (VulkanResult){.status = VULKAN_ERROR_INIT_FAILED, .vk_result = result};
    }

    vkGetDeviceQueue(app->device, graphicsIndex, 0, &app->queues.graphics);
    vkGetDeviceQueue(app->device, computeIndex, 0, &app->queues.compute);
    vkGetDeviceQueue(app->device, transferIndex, 0, &app->queues.transfer);

    app->queues.graphicsFamilyIndex = graphicsIndex;
    app->queues.computeFamilyIndex = computeIndex;
    app->queues.transferFamilyIndex = transferIndex;

    return (VulkanResult){.status = VULKAN_SUCCESS, .vk_result = VK_SUCCESS};
}

static VulkanResult create_surface(Application *app)
{
    if (glfwCreateWindowSurface(app->instance, app->window, NULL, &app->surface) != 0)
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

static VulkanResult create_swap_chain(Application *app)
{
    VkSurfaceCapabilitiesKHR surfaceCapabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(app->physicalDevice, app->surface, &surfaceCapabilities);

    app->swapChainExtent = choose_swap_extent(&surfaceCapabilities, app->window);
    uint32_t minImageCount = choose_swap_min_image_count(&surfaceCapabilities);

    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(app->physicalDevice, app->surface, &formatCount, NULL);
    VkSurfaceFormatKHR *availableFormats = malloc(formatCount * sizeof(VkSurfaceFormatKHR));
    if (availableFormats == NULL)
    {
        return (VulkanResult){.status = VULKAN_ERROR_OUT_OF_MEMORY, .vk_result = VK_SUCCESS};
    }
    vkGetPhysicalDeviceSurfaceFormatsKHR(app->physicalDevice, app->surface, &formatCount, availableFormats);
    app->swapChainSurfaceFormat = choose_swap_surface_format(availableFormats, formatCount);
    free(availableFormats);

    uint32_t presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(app->physicalDevice, app->surface, &presentModeCount, NULL);
    VkPresentModeKHR *availablePresentModes = malloc(presentModeCount * sizeof(VkPresentModeKHR));
    if (availablePresentModes == NULL)
    {
        return (VulkanResult){.status = VULKAN_ERROR_OUT_OF_MEMORY, .vk_result = VK_SUCCESS};
    }
    vkGetPhysicalDeviceSurfacePresentModesKHR(app->physicalDevice, app->surface, &presentModeCount, availablePresentModes);
    VkPresentModeKHR presentMode = choose_swap_present_mode(availablePresentModes, presentModeCount);
    free(availablePresentModes);

    VkSwapchainCreateInfoKHR swapChainCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .pNext = NULL,
        .flags = 0,
        .surface = app->surface,
        .minImageCount = minImageCount,
        .imageFormat = app->swapChainSurfaceFormat.format,
        .imageColorSpace = app->swapChainSurfaceFormat.colorSpace,
        .imageExtent = app->swapChainExtent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = NULL,
        .preTransform = surfaceCapabilities.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = presentMode,
        .clipped = VK_TRUE,
        .oldSwapchain = VK_NULL_HANDLE};

    VkResult create_result = vkCreateSwapchainKHR(app->device, &swapChainCreateInfo, NULL, &app->swapChain);
    if (create_result != VK_SUCCESS)
    {
        return (VulkanResult){.status = VULKAN_ERROR_SWAPCHAIN_CREATION_FAILED, .vk_result = create_result};
    }

    vkGetSwapchainImagesKHR(app->device, app->swapChain, &app->swapChainImageCount, NULL);
    app->swapChainImages = malloc(app->swapChainImageCount * sizeof(VkImage));
    if (app->swapChainImages == NULL)
    {
        return (VulkanResult){.status = VULKAN_ERROR_OUT_OF_MEMORY, .vk_result = VK_SUCCESS};
    }
    vkGetSwapchainImagesKHR(app->device, app->swapChain, &app->swapChainImageCount, app->swapChainImages);

    return (VulkanResult){.status = VULKAN_SUCCESS, .vk_result = VK_SUCCESS};
}

static VulkanResult create_image_views(Application *app)
{
    app->swapChainImageViewCount = app->swapChainImageCount;
    app->swapChainImageViews = malloc(app->swapChainImageViewCount * sizeof(VkImageView));
    if (app->swapChainImageViews == NULL)
    {
        return (VulkanResult){.status = VULKAN_ERROR_OUT_OF_MEMORY, .vk_result = VK_SUCCESS};
    }

    for (uint32_t i = 0; i < app->swapChainImageCount; i++)
    {
        VkImageViewCreateInfo createInfo = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .pNext = NULL,
            .flags = 0,
            .image = app->swapChainImages[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = app->swapChainSurfaceFormat.format,
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

        VkResult result = vkCreateImageView(app->device, &createInfo, NULL, &app->swapChainImageViews[i]);
        if (result != VK_SUCCESS)
        {
            return (VulkanResult){.status = VULKAN_ERROR_IMAGE_VIEW_CREATION_FAILED, .vk_result = result};
        }
    }

    return (VulkanResult){.status = VULKAN_SUCCESS, .vk_result = VK_SUCCESS};
}

VkShaderModule create_shader_module(VkDevice device, const char* code, size_t code_size) {
    VkShaderModuleCreateInfo createInfo = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .codeSize = code_size,
        .pCode = (const uint32_t*)code
    };

    VkShaderModule shaderModule;
    if (vkCreateShaderModule(device, &createInfo, NULL, &shaderModule) != VK_SUCCESS) {
        fprintf(stderr, "Failed to create shader module!\n");
        return VK_NULL_HANDLE;
    }

    return shaderModule;
}

static VulkanResult create_graphics_pipeline(Application* app) {
    size_t shaderSize = 0;
    char* shaderCode = read_file("shaders/slang.spv", &shaderSize);
    
    if (!shaderCode) {
        return (VulkanResult){.status = VULKAN_ERROR_FILE_READ_FAILED, .vk_result = VK_ERROR_UNKNOWN};
    }

    VkShaderModule shaderModule = create_shader_module(app->device, shaderCode, shaderSize);
    free(shaderCode);

    if (shaderModule == VK_NULL_HANDLE) {
        return (VulkanResult){.status = VULKAN_ERROR_SHADER_MODULE_CREATION_FAILED, .vk_result = VK_ERROR_UNKNOWN};
    }

    VkPipelineShaderStageCreateInfo vertShaderStageInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .stage = VK_SHADER_STAGE_VERTEX_BIT,
        .module = shaderModule,
        .pName = "vertMain",
        .pSpecializationInfo = NULL
    };

    VkPipelineShaderStageCreateInfo fragShaderStageInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
        .module = shaderModule,
        .pName = "fragMain",
        .pSpecializationInfo = NULL
    };

    VkPipelineShaderStageCreateInfo shaderStages[] = { vertShaderStageInfo, fragShaderStageInfo };

    VkPipelineVertexInputStateCreateInfo vertexInputInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .vertexBindingDescriptionCount = 0,
        .pVertexBindingDescriptions = NULL,
        .vertexAttributeDescriptionCount = 0,
        .pVertexAttributeDescriptions = NULL
    };

    VkPipelineInputAssemblyStateCreateInfo inputAssembly = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .primitiveRestartEnable = VK_FALSE
    };

    VkDynamicState dynamicStates[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };
    
    VkPipelineDynamicStateCreateInfo dynamicStateCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .dynamicStateCount = sizeof(dynamicStates) / sizeof(dynamicStates[0]),
        .pDynamicStates = dynamicStates
    };

    VkPipelineViewportStateCreateInfo viewportState = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .viewportCount = 1,
        .pViewports = NULL, 
        .scissorCount = 1,
        .pScissors = NULL   
    };

    VkPipelineRasterizationStateCreateInfo rasterizer = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .depthClampEnable = VK_FALSE,
        .rasterizerDiscardEnable = VK_FALSE,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_BACK_BIT,
        .frontFace = VK_FRONT_FACE_CLOCKWISE,
        .depthBiasEnable = VK_FALSE,
        .depthBiasConstantFactor = 0.0f,
        .depthBiasClamp = 0.0f,
        .depthBiasSlopeFactor = 0.0f,
        .lineWidth = 1.0f
    };

    VkPipelineMultisampleStateCreateInfo multisampling = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
        .sampleShadingEnable = VK_FALSE,
        .minSampleShading = 1.0f,
        .pSampleMask = NULL,
        .alphaToCoverageEnable = VK_FALSE,
        .alphaToOneEnable = VK_FALSE
    };

    VkPipelineColorBlendAttachmentState colorBlendAttachment = {
        .blendEnable = VK_FALSE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ZERO,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | 
                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
    };

    VkPipelineColorBlendStateCreateInfo colorBlending = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .logicOpEnable = VK_FALSE,
        .logicOp = VK_LOGIC_OP_COPY,
        .attachmentCount = 1,
        .pAttachments = &colorBlendAttachment,
        .blendConstants = {0.0f, 0.0f, 0.0f, 0.0f}
    };

    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .setLayoutCount = 0,
        .pSetLayouts = NULL,
        .pushConstantRangeCount = 0,
        .pPushConstantRanges = NULL
    };

    VkResult layoutResult = vkCreatePipelineLayout(app->device, &pipelineLayoutInfo, NULL, &app->pipelineLayout);
    if (layoutResult != VK_SUCCESS) {
        vkDestroyShaderModule(app->device, shaderModule, NULL);
        return (VulkanResult){.status = VULKAN_ERROR_PIPELINE_LAYOUT_CREATION_FAILED, .vk_result = layoutResult};
    }

    VkPipelineRenderingCreateInfo pipelineRenderingCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .pNext = NULL,
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &app->swapChainSurfaceFormat.format,
        .depthAttachmentFormat = VK_FORMAT_UNDEFINED,
        .stencilAttachmentFormat = VK_FORMAT_UNDEFINED
    };

    VkGraphicsPipelineCreateInfo pipelineInfo = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &pipelineRenderingCreateInfo,
        .flags = 0,
        .stageCount = 2,
        .pStages = shaderStages,
        .pVertexInputState = &vertexInputInfo,
        .pInputAssemblyState = &inputAssembly,
        .pTessellationState = NULL,
        .pViewportState = &viewportState,
        .pRasterizationState = &rasterizer,
        .pMultisampleState = &multisampling,
        .pDepthStencilState = NULL,
        .pColorBlendState = &colorBlending,
        .pDynamicState = &dynamicStateCreateInfo,
        .layout = app->pipelineLayout,
        .renderPass = VK_NULL_HANDLE,
        .subpass = 0,
        .basePipelineHandle = VK_NULL_HANDLE,
        .basePipelineIndex = -1
    };

    VkResult pipelineResult = vkCreateGraphicsPipelines(app->device, VK_NULL_HANDLE, 1, &pipelineInfo, NULL, &app->graphicsPipeline);
    vkDestroyShaderModule(app->device, shaderModule, NULL);
    if (pipelineResult != VK_SUCCESS) {
        return (VulkanResult){.status = VULKAN_ERROR_PIPELINE_CREATION_FAILED, .vk_result = pipelineResult};
    }

    return (VulkanResult){.status = VULKAN_SUCCESS, .vk_result = VK_SUCCESS};
}

static VulkanResult create_command_pool(Application* app) {
    VkCommandPoolCreateInfo poolInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .pNext = NULL,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = app->queues.graphicsFamilyIndex
    };
    VkResult poolResult = vkCreateCommandPool(app->device, &poolInfo, NULL, &app->commandPool);
    if (poolResult != VK_SUCCESS) {
        return (VulkanResult){.status = VULKAN_ERROR_COMMAND_POOL_CREATION_FAILED, .vk_result = poolResult};
    }
    return (VulkanResult){.status = VULKAN_SUCCESS, .vk_result = VK_SUCCESS};
}

static VulkanResult create_command_buffer(Application* app) {
    VkCommandBufferAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .pNext = NULL,
        .commandPool = app->commandPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1
    };
    VkResult result = vkAllocateCommandBuffers(app->device, &allocInfo, &app->commandBuffer);
    if (result != VK_SUCCESS) {
        return (VulkanResult){.status = VULKAN_ERROR_COMMAND_BUFFER_CREATION_FAILED, .vk_result = result};
    }
    return (VulkanResult){.status = VULKAN_SUCCESS, .vk_result = VK_SUCCESS};
}

void transition_image_layout(
    Application* app,
    uint32_t                 imageIndex,
    VkImageLayout            old_layout,
    VkImageLayout            new_layout,
    VkAccessFlags2           src_access_mask,
    VkAccessFlags2           dst_access_mask,
    VkPipelineStageFlags2    src_stage_mask,
    VkPipelineStageFlags2    dst_stage_mask)
{
    VkImageMemoryBarrier2 barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .pNext = NULL,
        .srcStageMask = src_stage_mask,
        .srcAccessMask = src_access_mask,
        .dstStageMask = dst_stage_mask,
        .dstAccessMask = dst_access_mask,
        .oldLayout = old_layout,
        .newLayout = new_layout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = app->swapChainImages[imageIndex],
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1
        }
    };

    VkDependencyInfo dependencyInfo = {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .pNext = NULL,
        .dependencyFlags = 0,
        .memoryBarrierCount = 0,
        .pMemoryBarriers = NULL,
        .bufferMemoryBarrierCount = 0,
        .pBufferMemoryBarriers = NULL,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &barrier
    };

    vkCmdPipelineBarrier2(app->commandBuffer, &dependencyInfo);
}

VulkanResult record_command_buffer(Application* app, uint32_t imageIndex) {
    VkCommandBufferBeginInfo beginInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .pNext = NULL,
        .flags = 0,
        .pInheritanceInfo = NULL
    };

    VkResult beginResult = vkBeginCommandBuffer(app->commandBuffer, &beginInfo);
    if (beginResult != VK_SUCCESS) {
        return (VulkanResult){.status = VULKAN_ERROR_COMMAND_BUFFER_FAILED_BEGIN, .vk_result = beginResult};
    }

    transition_image_layout(
        app,
        imageIndex,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        0,                                                  // srcAccessMask
        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,             // dstAccessMask
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,    // srcStageMask
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT     // dstStageMask
    );

    VkClearValue clearColor = {
        .color = { .float32 = {0.0f, 0.0f, 0.0f, 1.0f} }
    };

    VkRenderingAttachmentInfo attachmentInfo = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .pNext = NULL,
        .imageView = app->swapChainImageViews[imageIndex],
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .resolveMode = VK_RESOLVE_MODE_NONE,
        .resolveImageView = VK_NULL_HANDLE,
        .resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue = clearColor
    };

    VkRenderingInfo renderingInfo = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .pNext = NULL,
        .flags = 0,
        .renderArea = {
            .offset = {0, 0}, 
            .extent = app->swapChainExtent
        },
        .layerCount = 1,
        .viewMask = 0,
        .colorAttachmentCount = 1,
        .pColorAttachments = &attachmentInfo,
        .pDepthAttachment = NULL,
        .pStencilAttachment = NULL
    };

    vkCmdBeginRendering(app->commandBuffer, &renderingInfo);
    vkCmdBindPipeline(app->commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, app->graphicsPipeline);

    VkViewport viewport = {
        .x = 0.0f,
        .y = 0.0f,
        .width = (float)app->swapChainExtent.width,
        .height = (float)app->swapChainExtent.height,
        .minDepth = 0.0f,
        .maxDepth = 1.0f
    };
    vkCmdSetViewport(app->commandBuffer, 0, 1, &viewport);

    VkRect2D scissor = {
        .offset = {0, 0},
        .extent = app->swapChainExtent
    };
    vkCmdSetScissor(app->commandBuffer, 0, 1, &scissor);

    //Draw Call (3 vertices, 1 instance)
    vkCmdDraw(app->commandBuffer, 3, 1, 0, 0);

    vkCmdEndRendering(app->commandBuffer);

    transition_image_layout(
        app,
        imageIndex,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,             // srcAccessMask
        0,                                                  // dstAccessMask
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,    // srcStageMask
        VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT              // dstStageMask
    );

    VkResult endResult = vkEndCommandBuffer(app->commandBuffer);
    if (endResult != VK_SUCCESS) {
        return (VulkanResult){.status = VULKAN_ERROR_COMMAND_BUFFER_FAILED_END, .vk_result = endResult};
    }
    return (VulkanResult){.status = VULKAN_SUCCESS, .vk_result = VK_SUCCESS};
}

static VulkanResult create_sync_objects(Application* app) {
    VkSemaphoreCreateInfo semaphoreInfo = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
    };
    VkFenceCreateInfo fenceInfo = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT
    };

    vkCreateSemaphore(app->device, &semaphoreInfo, NULL, &app->presentCompleteSemaphore);
    vkCreateSemaphore(app->device, &semaphoreInfo, NULL, &app->renderFinishedSemaphore);
    vkCreateFence(app->device, &fenceInfo, NULL, &app->drawFence);
    return (VulkanResult){.status = VULKAN_SUCCESS, .vk_result = VK_SUCCESS};
}

static VulkanResult init_vulkan(Application *app)
{
    VulkanResult res = create_instance(app);
    if (res.status != VULKAN_SUCCESS)
        return res;
    res = setup_debug_messenger(app);
    if (res.status != VULKAN_SUCCESS)
        return res;
    res = create_surface(app);
    if (res.status != VULKAN_SUCCESS)
        return res;
    res = pick_physical_device(app);
    if (res.status != VULKAN_SUCCESS)
        return res;
    res = create_logical_device(app);
    if (res.status != VULKAN_SUCCESS)
        return res;
    res = create_swap_chain(app);
    if (res.status != VULKAN_SUCCESS)
        return res;
    res = create_image_views(app);
    if (res.status != VULKAN_SUCCESS)
        return res;
    res = create_graphics_pipeline(app);
    if (res.status != VULKAN_SUCCESS)
        return res;
    res = create_command_pool(app);
    if (res.status != VULKAN_SUCCESS)
        return res;
    res = create_command_buffer(app);
    if (res.status != VULKAN_SUCCESS)
        return res;
    res = create_sync_objects(app);
    return res;
}

static VulkanResult draw_frame(Application* app) {
    VkResult result = vkWaitForFences(app->device, 1, &app->drawFence, VK_TRUE, UINT64_MAX);
    if (result != VK_SUCCESS) {
        return (VulkanResult){.status = VULKAN_ERROR_FENCE_WAIT_FAILED, .vk_result = result};
    }
    vkResetFences(app->device, 1, &app->drawFence);
    uint32_t imageIndex;
    result = vkAcquireNextImageKHR(
        app->device,
        app->swapChain,
        UINT64_MAX,
        app->presentCompleteSemaphore,
        VK_NULL_HANDLE,
        &imageIndex
    );
    if (result != VK_SUCCESS) {
        return (VulkanResult){.status = VULKAN_ERROR_SWAPCHAIN_NEXT_IMAGE_FAILED, .vk_result = result};
    }
    record_command_buffer(app, imageIndex);

    VkPipelineStageFlags waitDestinationStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submitInfo = {0};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &app->presentCompleteSemaphore;
    submitInfo.pWaitDstStageMask = &waitDestinationStageMask;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &app->commandBuffer;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &app->renderFinishedSemaphore;

    result = vkQueueSubmit(
        app->queues.graphics,
        1,
        &submitInfo,
        app->drawFence
    );
    if (result != VK_SUCCESS) {
        return (VulkanResult){.status = VULKAN_ERROR_QUEUE_SUBMIT_FAILED, .vk_result = result};
    }
    VkPresentInfoKHR presentInfo = {0};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &app->renderFinishedSemaphore;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &app->swapChain;
    presentInfo.pImageIndices = &imageIndex;
    presentInfo.pResults = NULL;

    result = vkQueuePresentKHR(app->queues.graphics, &presentInfo);
    if (result != VK_SUCCESS) {
        return (VulkanResult){.status = VULKAN_ERROR_QUEUE_PRESENT_FAILED, .vk_result = result};
    }
    return (VulkanResult){.status = VULKAN_SUCCESS, .vk_result = VK_SUCCESS};
}

static VulkanResult main_loop(Application *app)
{
    while (!glfwWindowShouldClose(app->window))
    {
        glfwPollEvents();
        VulkanResult res = draw_frame(app);
        if (res.status != VULKAN_SUCCESS)
            return res;
        vkDeviceWaitIdle(app->device);
    }
    return (VulkanResult){.status = VULKAN_SUCCESS, .vk_result = VK_SUCCESS};
}

static void cleanup(Application *app)
{
    if (app->presentCompleteSemaphore != VK_NULL_HANDLE) {
        vkDestroySemaphore(app->device, app->presentCompleteSemaphore, NULL);
    }
    if (app->renderFinishedSemaphore != VK_NULL_HANDLE) {
        vkDestroySemaphore(app->device, app->renderFinishedSemaphore, NULL);
    }
    if (app->drawFence != VK_NULL_HANDLE) {
        vkDestroyFence(app->device, app->drawFence, NULL);
    }
    if (app->commandPool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(app->device, app->commandPool, NULL);
    }
    if (app->graphicsPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(app->device, app->graphicsPipeline, NULL);
    }
    
    if (app->pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(app->device, app->pipelineLayout, NULL);
    }
    if (app->swapChainImageViews != NULL)
    {
        for (uint32_t i = 0; i < app->swapChainImageViewCount; i++)
        {
            if (app->swapChainImageViews[i] != VK_NULL_HANDLE)
            {
                vkDestroyImageView(app->device, app->swapChainImageViews[i], NULL);
            }
        }
        free(app->swapChainImageViews);
        app->swapChainImageViews = NULL;
    }
    if (app->swapChainImages != NULL)
    {
        free(app->swapChainImages);
        app->swapChainImages = NULL;
    }
    if (app->swapChain != VK_NULL_HANDLE)
    {
        vkDestroySwapchainKHR(app->device, app->swapChain, NULL);
        app->swapChain = VK_NULL_HANDLE;
    }
    if (app->device != VK_NULL_HANDLE)
    {
        vkDestroyDevice(app->device, NULL);
        app->device = VK_NULL_HANDLE;
    }
    if (app->instance != VK_NULL_HANDLE && app->surface != VK_NULL_HANDLE)
    {
        vkDestroySurfaceKHR(app->instance, app->surface, NULL);
        app->surface = VK_NULL_HANDLE;
    }
#ifdef ENABLE_VALIDATION_LAYERS
    if (app->instance != VK_NULL_HANDLE && app->debugMessenger != VK_NULL_HANDLE)
    {
        destroy_debug_utils_messenger_ext(app->instance, app->debugMessenger, NULL);
        app->debugMessenger = VK_NULL_HANDLE;
    }
#endif
    if (app->instance != VK_NULL_HANDLE)
    {
        vkDestroyInstance(app->instance, NULL);
        app->instance = VK_NULL_HANDLE;
    }
    if (app->window != NULL)
    {
        glfwDestroyWindow(app->window);
        app->window = NULL;
    }
    glfwTerminate();
}

static VulkanResult app_run(Application *app)
{
    app->window = NULL;
    app->instance = VK_NULL_HANDLE;
    app->physicalDevice = VK_NULL_HANDLE;
    app->device = VK_NULL_HANDLE;
    app->debugMessenger = VK_NULL_HANDLE;
    app->surface = VK_NULL_HANDLE;
    app->swapChain = VK_NULL_HANDLE;
    app->swapChainImageCount = 0;
    app->swapChainImages = NULL;

    VulkanResult res = init_window(app);
    if (res.status != VULKAN_SUCCESS)
    {
        cleanup(app);
        return res;
    }

    res = init_vulkan(app);
    if (res.status != VULKAN_SUCCESS)
    {
        cleanup(app);
        return res;
    }

    res = main_loop(app);
    if (res.status != VULKAN_SUCCESS)
    {
        cleanup(app);
        return res;
    }

    cleanup(app);
    return (VulkanResult){.status = VULKAN_SUCCESS, .vk_result = VK_SUCCESS};
}


int main()
{
    Application app;
    const VulkanResult result = app_run(&app);

    if (result.status != VULKAN_SUCCESS)
    {
        char error_message[512];

        snprintf(error_message, sizeof(error_message),
                 "The application encountered a fatal condition and must close.\n\n"
                 "Internal Status: %d\n"
                 "Vulkan Status: %d",
                 result.status,
                 result.vk_result);

        printf("%s\n", error_message);
        tinyfd_messageBox("Vulkan Initialization Failed", error_message, "ok", "error", 1);

        return -1;
    }

    printf("App exited clean!\n");
    return 0;
}