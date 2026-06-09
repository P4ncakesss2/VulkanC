#include "vulkan_ctx.h"
#include "window.h"

int main() {
    VkContext context;
    VkContextCreateInfo createInfo = {
        .appName = "Test App",
        .enablePresentation = true,
        .validationLayers = true,
    };

    VulkanResult result = vkContextCreate(&createInfo, &context);
    if (result.status != VK_SUCCESS) {
        // blah blah blah
    }

    VkWindow window;
    VkWindowCreateInfo windowInfo = {
        .title = "Test App",
        .height = 600,
        .width = 800,
    };

    result = vkWindowCreate(&context, &windowInfo, &window);
    if (result.status != VK_SUCCESS) {
        // blah blah blah
    }

    while(!vkWindowShouldClose(&window)) {
        vkPollEvents();
    }
    
    vkWindowDestroy(&context, &window);
    vkContextDestroy(&context);

    return 0;
}