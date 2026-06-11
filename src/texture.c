#include "texture.h"
#include "logger.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

static VulkanResult transition_image_layout(
    VkContext*   ctx,
    VkCommandBuffer cmd,
    VkImage      image,
    VkImageLayout oldLayout,
    VkImageLayout newLayout,
    VkPipelineStageFlags2 srcStage,
    VkAccessFlags2        srcAccess,
    VkPipelineStageFlags2 dstStage,
    VkAccessFlags2        dstAccess)
{
    VkImageMemoryBarrier2 barrier = {
        .sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .pNext            = NULL,
        .srcStageMask     = srcStage,
        .srcAccessMask    = srcAccess,
        .dstStageMask     = dstStage,
        .dstAccessMask    = dstAccess,
        .oldLayout        = oldLayout,
        .newLayout        = newLayout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image            = image,
        .subresourceRange = {
            .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel   = 0,
            .levelCount     = 1,
            .baseArrayLayer = 0,
            .layerCount     = 1,
        },
    };
    VkDependencyInfo dep = {
        .sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers    = &barrier,
    };
    vkCmdPipelineBarrier2(cmd, &dep);
    return (VulkanResult){.status = VULKAN_SUCCESS, .vk_result = VK_SUCCESS};
}

static VulkanResult begin_upload_cmd(VkContext* ctx, VkCommandBuffer* outCmd) {
    VkCommandBufferAllocateInfo allocInfo = {
        .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool        = ctx->transferPool,
        .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    VkResult result = vkAllocateCommandBuffers(ctx->logicalDevice, &allocInfo, outCmd);
    if (result != VK_SUCCESS) {
        LOG_ERROR("vkAllocateCommandBuffers failed for upload cmd. VkResult: %i", result);
        return (VulkanResult){.status = VULKAN_ERROR_COMMAND_BUFFER_ALLOCATION_FAILED, .vk_result = result};
    }
    VkCommandBufferBeginInfo beginInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    result = vkBeginCommandBuffer(*outCmd, &beginInfo);
    if (result != VK_SUCCESS) {
        LOG_ERROR("vkBeginCommandBuffer failed for upload cmd. VkResult: %i", result);
        return (VulkanResult){.status = VULKAN_ERROR_COMMAND_BUFFER_FAILED_BEGIN, .vk_result = result};
    }
    return (VulkanResult){.status = VULKAN_SUCCESS, .vk_result = VK_SUCCESS};
}

static VulkanResult end_and_submit_upload_cmd(VkContext* ctx, VkCommandBuffer cmd) {
    VkResult result = vkEndCommandBuffer(cmd);
    if (result != VK_SUCCESS) {
        LOG_ERROR("vkEndCommandBuffer failed for upload cmd. VkResult: %i", result);
        return (VulkanResult){.status = VULKAN_ERROR_COMMAND_BUFFER_FAILED_END, .vk_result = result};
    }
    VkSubmitInfo submit = {
        .sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers    = &cmd,
    };
    result = vkQueueSubmit(ctx->queues.transfer, 1, &submit, VK_NULL_HANDLE);
    if (result != VK_SUCCESS) {
        LOG_ERROR("vkQueueSubmit failed for upload cmd. VkResult: %i", result);
        return (VulkanResult){.status = VULKAN_ERROR_QUEUE_SUBMIT_FAILED, .vk_result = result};
    }
    vkQueueWaitIdle(ctx->queues.transfer);
    vkFreeCommandBuffers(ctx->logicalDevice, ctx->transferPool, 1, &cmd);
    return (VulkanResult){.status = VULKAN_SUCCESS, .vk_result = VK_SUCCESS};
}

VulkanResult vkTextureCreate(VkContext* ctx, const char* path, VkTexture* out) {
    // ---- load pixels ----
    int width, height, channels;
    stbi_uc* pixels = stbi_load(path, &width, &height, &channels, STBI_rgb_alpha);
    if (!pixels) {
        LOG_ERROR("stb_image failed to load texture: %s", path);
        return (VulkanResult){.status = VULKAN_ERROR_IMAGE_CREATION_FAILED, .vk_result = VK_ERROR_UNKNOWN};
    }
    VkDeviceSize imageSize = (VkDeviceSize)(width * height * 4);

    // ---- staging buffer ----
    VkBuffer      stagingBuffer;
    VmaAllocation stagingAlloc;
    VkBufferCreateInfo stagingBufInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size  = imageSize,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
    };
    VmaAllocationCreateInfo stagingAllocInfo = {
        .usage = VMA_MEMORY_USAGE_AUTO,
        .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                 VMA_ALLOCATION_CREATE_MAPPED_BIT,
    };
    VmaAllocationInfo stagingAllocResult;
    VkResult result = vmaCreateBuffer(ctx->allocator, &stagingBufInfo, &stagingAllocInfo,
        &stagingBuffer, &stagingAlloc, &stagingAllocResult);
    if (result != VK_SUCCESS) {
        stbi_image_free(pixels);
        LOG_ERROR("vmaCreateBuffer failed for texture staging. VkResult: %i", result);
        return (VulkanResult){.status = VULKAN_ERROR_BUFFER_CREATION_FAILED, .vk_result = result};
    }
    memcpy(stagingAllocResult.pMappedData, pixels, (size_t)imageSize);
    stbi_image_free(pixels);

    // ---- device-local image ----
    VkImageCreateInfo imageInfo = {
        .sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType     = VK_IMAGE_TYPE_2D,
        .format        = VK_FORMAT_R8G8B8A8_SRGB,
        .extent        = { (uint32_t)width, (uint32_t)height, 1 },
        .mipLevels     = 1,
        .arrayLayers   = 1,
        .samples       = VK_SAMPLE_COUNT_1_BIT,
        .tiling        = VK_IMAGE_TILING_OPTIMAL,
        .usage         = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .sharingMode   = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    VmaAllocationCreateInfo imageAllocInfo = {
        .usage = VMA_MEMORY_USAGE_AUTO,
        .flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT,
    };
    result = vmaCreateImage(ctx->allocator, &imageInfo, &imageAllocInfo,
        &out->image, &out->allocation, NULL);
    if (result != VK_SUCCESS) {
        vmaDestroyBuffer(ctx->allocator, stagingBuffer, stagingAlloc);
        LOG_ERROR("vmaCreateImage failed for texture. VkResult: %i", result);
        return (VulkanResult){.status = VULKAN_ERROR_IMAGE_CREATION_FAILED, .vk_result = result};
    }
    out->width  = (uint32_t)width;
    out->height = (uint32_t)height;

    // ---- upload via one-shot command buffer ----
    VkCommandBuffer cmd;
    VulkanResult vr = begin_upload_cmd(ctx, &cmd);
    if (vr.status != VULKAN_SUCCESS) {
        vmaDestroyBuffer(ctx->allocator, stagingBuffer, stagingAlloc);
        vmaDestroyImage(ctx->allocator, out->image, out->allocation);
        return vr;
    }

    // UNDEFINED -> TRANSFER_DST
    transition_image_layout(ctx, cmd, out->image,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_PIPELINE_STAGE_2_NONE,         0,
        VK_PIPELINE_STAGE_2_COPY_BIT,     VK_ACCESS_2_TRANSFER_WRITE_BIT);

    VkBufferImageCopy region = {
        .bufferOffset      = 0,
        .bufferRowLength   = 0,
        .bufferImageHeight = 0,
        .imageSubresource  = {
            .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
            .mipLevel       = 0,
            .baseArrayLayer = 0,
            .layerCount     = 1,
        },
        .imageOffset = { 0, 0, 0 },
        .imageExtent = { (uint32_t)width, (uint32_t)height, 1 },
    };
    vkCmdCopyBufferToImage(cmd, stagingBuffer, out->image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    // TRANSFER_DST -> SHADER_READ_ONLY
    transition_image_layout(ctx, cmd, out->image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_2_COPY_BIT,           VK_ACCESS_2_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT);

    vr = end_and_submit_upload_cmd(ctx, cmd);
    vmaDestroyBuffer(ctx->allocator, stagingBuffer, stagingAlloc);
    if (vr.status != VULKAN_SUCCESS) {
        vmaDestroyImage(ctx->allocator, out->image, out->allocation);
        return vr;
    }

    // ---- image view ----
    VkImageViewCreateInfo viewInfo = {
        .sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image    = out->image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format   = VK_FORMAT_R8G8B8A8_SRGB,
        .subresourceRange = {
            .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel   = 0,
            .levelCount     = 1,
            .baseArrayLayer = 0,
            .layerCount     = 1,
        },
    };
    result = vkCreateImageView(ctx->logicalDevice, &viewInfo, NULL, &out->imageView);
    if (result != VK_SUCCESS) {
        vmaDestroyImage(ctx->allocator, out->image, out->allocation);
        LOG_ERROR("vkCreateImageView failed for texture. VkResult: %i", result);
        return (VulkanResult){.status = VULKAN_ERROR_IMAGE_VIEW_CREATION_FAILED, .vk_result = result};
    }

    LOG_INFO("Texture loaded: %s (%ux%u)", path, out->width, out->height);
    return (VulkanResult){.status = VULKAN_SUCCESS, .vk_result = VK_SUCCESS};
}

uint32_t vkTextureRegister(VkContext* ctx, VkTexture* tex) {
    uint32_t slot = ctx->textureCount++;

    VkDescriptorImageInfo imageInfo = {
        .sampler     = VK_NULL_HANDLE,
        .imageView   = tex->imageView,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };

    // write into every frame's descriptor set
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkWriteDescriptorSet write = {
            .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet          = ctx->globalDescriptorSets[i],
            .dstBinding      = 1,
            .dstArrayElement = slot,
            .descriptorCount = 1,
            .descriptorType  = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            .pImageInfo      = &imageInfo,
        };
        vkUpdateDescriptorSets(ctx->logicalDevice, 1, &write, 0, NULL);
    }

    LOG_INFO("Texture registered at bindless slot %u", slot);
    return slot;
}

VulkanResult vkTextureLoad(VkContext* ctx, const char* path, VkTexture* out, uint32_t* outSlot) {
    VulkanResult result = vkTextureCreate(ctx, path, out);
    if (result.status != VULKAN_SUCCESS) return result;
    *outSlot = vkTextureRegister(ctx, out);
    return (VulkanResult){.status = VULKAN_SUCCESS, .vk_result = VK_SUCCESS};
}

void vkTextureDestroy(VkContext* ctx, VkTexture* tex) {
    if (tex->imageView != VK_NULL_HANDLE) {
        vkDestroyImageView(ctx->logicalDevice, tex->imageView, NULL);
        tex->imageView = VK_NULL_HANDLE;
    }
    if (tex->image != VK_NULL_HANDLE) {
        vmaDestroyImage(ctx->allocator, tex->image, tex->allocation);
        tex->image      = VK_NULL_HANDLE;
        tex->allocation = VK_NULL_HANDLE;
    }
}