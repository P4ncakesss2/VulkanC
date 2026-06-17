#include "texture.h"
#include "logger.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

static uint32_t compute_mip_levels(uint32_t width, uint32_t height) {
    uint32_t levels = 1;
    uint32_t w = width, h = height;
    while (w > 1 || h > 1) { w >>= 1; h >>= 1; levels++; }
    return levels;
}

static VulkanResult transition_image_layout(
    VkContext* ctx,
    VkCommandBuffer       cmd,
    VkImage               image,
    VkImageLayout         oldLayout,
    VkImageLayout         newLayout,
    VkPipelineStageFlags2 srcStage,
    VkAccessFlags2        srcAccess,
    VkPipelineStageFlags2 dstStage,
    VkAccessFlags2        dstAccess,
    uint32_t              baseMipLevel,
    uint32_t              levelCount,
    uint32_t              layerCount)
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
            .baseMipLevel   = baseMipLevel,
            .levelCount     = levelCount,
            .baseArrayLayer = 0,
            .layerCount     = layerCount,
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

static VulkanResult generate_mipmaps(
    VkContext* ctx,
    VkCommandBuffer cmd,
    VkImage         image,
    uint32_t        width,
    uint32_t        height,
    uint32_t        mipLevels,
    uint32_t        layerCount)
{
// Replace loop & final transition in generate_mipmaps inside texture.c
for (uint32_t i = 1; i < mipLevels; i++) {
    // Fix: Level 0 is populated via COPY, subsequent levels are populated via BLIT
    VkPipelineStageFlags2 srcStage = (i == 1) ? VK_PIPELINE_STAGE_2_COPY_BIT : VK_PIPELINE_STAGE_2_BLIT_BIT;

    transition_image_layout(ctx, cmd, image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        srcStage,                     VK_ACCESS_2_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
        i - 1, 1, layerCount);

    int32_t srcW = (int32_t)(width  >> (i - 1)); if (srcW < 1) srcW = 1;
    int32_t srcH = (int32_t)(height >> (i - 1)); if (srcH < 1) srcH = 1;
    int32_t dstW = srcW > 1 ? srcW >> 1 : 1;
    int32_t dstH = srcH > 1 ? srcH >> 1 : 1;

    VkImageBlit blit = {
        .srcSubresource = {
            .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
            .mipLevel       = i - 1,
            .baseArrayLayer = 0,
            .layerCount     = layerCount,
        },
        .srcOffsets = { { 0, 0, 0 }, { srcW, srcH, 1 } },
        .dstSubresource = {
            .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
            .mipLevel       = i,
            .baseArrayLayer = 0,
            .layerCount     = layerCount,
        },
        .dstOffsets = { { 0, 0, 0 }, { dstW, dstH, 1 } },
    };
    vkCmdBlitImage(cmd,
        image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1, &blit, VK_FILTER_LINEAR);

    transition_image_layout(ctx, cmd, image,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_2_BLIT_BIT,            VK_ACCESS_2_TRANSFER_READ_BIT,
        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT,
        i - 1, 1, layerCount);
}

// Fix: Transition the final mip level which was altered via BLIT (if mipCount > 1)
VkPipelineStageFlags2 finalSrcStage = (mipLevels > 1) ? VK_PIPELINE_STAGE_2_BLIT_BIT : VK_PIPELINE_STAGE_2_COPY_BIT;

transition_image_layout(ctx, cmd, image,
    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    finalSrcStage,                           VK_ACCESS_2_TRANSFER_WRITE_BIT,
    VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT,
    mipLevels - 1, 1, layerCount);

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

VulkanResult vkTextureCreate(VkContext* ctx, const char* path, VkTexture* out, bool isLinear) {
    int width, height, channels;
    void* pixels = NULL;
    bool isHDR = stbi_is_hdr(path);
    VkFormat format;
    VkDeviceSize imageSize;
    if (isLinear) {
        pixels = stbi_load(path, &width, &height, &channels, STBI_rgb_alpha);
        format = VK_FORMAT_R8G8B8A8_UNORM;
        imageSize = (VkDeviceSize)(width * height * 4);
    } else if (isHDR) {
        pixels = stbi_loadf(path, &width, &height, &channels, STBI_rgb_alpha);
        format = VK_FORMAT_R32G32B32A32_SFLOAT;
        imageSize = (VkDeviceSize)(width * height * 4 * sizeof(float));
    } else {
        pixels = stbi_load(path, &width, &height, &channels, STBI_rgb_alpha);
        format = VK_FORMAT_R8G8B8A8_SRGB;
        imageSize = (VkDeviceSize)(width * height * 4);
    }

    if (!pixels) {
        LOG_ERROR("stb_image failed to load texture: %s", path);
        return (VulkanResult){.status = VULKAN_ERROR_IMAGE_CREATION_FAILED, .vk_result = VK_ERROR_UNKNOWN};
    }

    VkFormatProperties fmtProps;
    vkGetPhysicalDeviceFormatProperties(ctx->physicalDevice, format, &fmtProps);
    if (!(fmtProps.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)) {
        LOG_WARN("Format does not support linear filtering; mip generation may produce artefacts");
    }

    uint32_t mipLevels = compute_mip_levels((uint32_t)width, (uint32_t)height);

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

    VkImageCreateInfo imageInfo = {
        .sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType     = VK_IMAGE_TYPE_2D,
        .format        = format,
        .extent        = { (uint32_t)width, (uint32_t)height, 1 },
        .mipLevels     = mipLevels,
        .arrayLayers   = 1,
        .samples       = VK_SAMPLE_COUNT_1_BIT,
        .tiling        = VK_IMAGE_TILING_OPTIMAL,
        .usage         = VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                         VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                         VK_IMAGE_USAGE_SAMPLED_BIT,
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
    out->width     = (uint32_t)width;
    out->height    = (uint32_t)height;
    out->mipLevels = mipLevels;

    VkCommandBuffer cmd;
    VulkanResult vr = begin_upload_cmd(ctx, &cmd);
    if (vr.status != VULKAN_SUCCESS) {
        vmaDestroyBuffer(ctx->allocator, stagingBuffer, stagingAlloc);
        vmaDestroyImage(ctx->allocator, out->image, out->allocation);
        return vr;
    }

    transition_image_layout(ctx, cmd, out->image,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_PIPELINE_STAGE_2_NONE, 0,
        VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
        0, mipLevels, 1);

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

    vr = generate_mipmaps(ctx, cmd,
        out->image, out->width, out->height, out->mipLevels, 1);

    vr = end_and_submit_upload_cmd(ctx, cmd);
    vmaDestroyBuffer(ctx->allocator, stagingBuffer, stagingAlloc);
    if (vr.status != VULKAN_SUCCESS) {
        vmaDestroyImage(ctx->allocator, out->image, out->allocation);
        return vr;
    }

    VkImageViewCreateInfo viewInfo = {
        .sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image    = out->image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format   = format,
        .subresourceRange = {
            .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel   = 0,
            .levelCount     = mipLevels,
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

    LOG_INFO("Texture loaded: %s (%ux%u, %u mip levels)", path, out->width, out->height, mipLevels);
    return (VulkanResult){.status = VULKAN_SUCCESS, .vk_result = VK_SUCCESS};
}

uint32_t vkTextureRegister(VkContext* ctx, VkTexture* tex, uint32_t binding) {
    uint32_t slot = ctx->textureCount++;

    VkDescriptorImageInfo imageInfo = {
        .sampler     = VK_NULL_HANDLE,
        .imageView   = tex->imageView,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkWriteDescriptorSet write = {
            .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet          = ctx->globalDescriptorSets[i],
            .dstBinding      = binding,
            .dstArrayElement = slot,
            .descriptorCount = 1,
            .descriptorType  = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            .pImageInfo      = &imageInfo,
        };
        vkUpdateDescriptorSets(ctx->logicalDevice, 1, &write, 0, NULL);
    }

    LOG_INFO("Texture registered at bindless slot %u on binding %u", slot, binding);
    return slot;
}

VulkanResult vkTextureLoad(VkContext* ctx, const char* path, VkTexture* out, uint32_t* outSlot, bool IsLinear) {
    VulkanResult result = vkTextureCreate(ctx, path, out, IsLinear);
    if (result.status != VULKAN_SUCCESS) return result;
    *outSlot = vkTextureRegister(ctx, out, 2);
    return (VulkanResult){.status = VULKAN_SUCCESS, .vk_result = VK_SUCCESS};
}

VulkanResult vkTextureCreateCubemap(VkContext* ctx, const char* paths[6], VkTexture* out) {
    int width = 0, height = 0, channels = 0;
    void* faces[6] = {NULL};
    bool isHDR = stbi_is_hdr(paths[0]);
    VkFormat format = isHDR ? VK_FORMAT_R32G32B32A32_SFLOAT : VK_FORMAT_R8G8B8A8_SRGB;
    VkDeviceSize faceSize = 0;

    for (int i = 0; i < 6; i++) {
        int w, h, c;
        if (isHDR) faces[i] = stbi_loadf(paths[i], &w, &h, &c, STBI_rgb_alpha);
        else       faces[i] = stbi_load(paths[i], &w, &h, &c, STBI_rgb_alpha);

        if (!faces[i]) {
            LOG_ERROR("Failed to load cubemap face %d: %s", i, paths[i]);
            for (int j = 0; j < i; j++) stbi_image_free(faces[j]);
            return (VulkanResult){.status = VULKAN_ERROR_IMAGE_CREATION_FAILED};
        }
        if (i == 0) {
            width = w; height = h;
            faceSize = isHDR ? (w * h * 4 * sizeof(float)) : (w * h * 4);
        } else if (w != width || h != height) {
            LOG_ERROR("Cubemap face dimensions mismatched!");
            for (int j = 0; j <= i; j++) stbi_image_free(faces[j]);
            return (VulkanResult){.status = VULKAN_ERROR_IMAGE_CREATION_FAILED};
        }
    }

    uint32_t mipLevels = compute_mip_levels((uint32_t)width, (uint32_t)height);
    VkDeviceSize totalImageSize = faceSize * 6;

    VkBuffer stagingBuffer; VmaAllocation stagingAlloc;
    VkBufferCreateInfo stagingBufInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = totalImageSize, .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
    };
    VmaAllocationCreateInfo stagingAllocInfo = {
        .usage = VMA_MEMORY_USAGE_AUTO, .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
    };
    VmaAllocationInfo stagingAllocResult;
    VkResult result = vmaCreateBuffer(ctx->allocator, &stagingBufInfo, &stagingAllocInfo, &stagingBuffer, &stagingAlloc, &stagingAllocResult);
    
    if (result != VK_SUCCESS) {
        for (int i = 0; i < 6; i++) stbi_image_free(faces[i]);
        return (VulkanResult){.status = VULKAN_ERROR_BUFFER_CREATION_FAILED, .vk_result = result};
    }

    uint8_t* pDst = (uint8_t*)stagingAllocResult.pMappedData;
    for (int i = 0; i < 6; i++) {
        memcpy(pDst + (i * faceSize), faces[i], (size_t)faceSize);
        stbi_image_free(faces[i]);
    }

    VkImageCreateInfo imageInfo = {
        .sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType     = VK_IMAGE_TYPE_2D,
        .format        = format,
        .extent        = { (uint32_t)width, (uint32_t)height, 1 },
        .mipLevels     = mipLevels,
        .arrayLayers   = 6,
        .samples       = VK_SAMPLE_COUNT_1_BIT,
        .tiling        = VK_IMAGE_TILING_OPTIMAL,
        .usage         = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .sharingMode   = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .flags         = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT, 
    };
    VmaAllocationCreateInfo imageAllocInfo = { .usage = VMA_MEMORY_USAGE_AUTO, .flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT };
    result = vmaCreateImage(ctx->allocator, &imageInfo, &imageAllocInfo, &out->image, &out->allocation, NULL);
    if (result != VK_SUCCESS) {
        vmaDestroyBuffer(ctx->allocator, stagingBuffer, stagingAlloc);
        return (VulkanResult){.status = VULKAN_ERROR_IMAGE_CREATION_FAILED, .vk_result = result};
    }

    out->width = (uint32_t)width; out->height = (uint32_t)height; out->mipLevels = mipLevels;

    VkCommandBuffer cmd;
    VulkanResult vr = begin_upload_cmd(ctx, &cmd);
    transition_image_layout(ctx, cmd, out->image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_NONE, 0, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, 0, mipLevels, 6);

    VkBufferImageCopy regions[6];
    for (uint32_t i = 0; i < 6; i++) {
        regions[i] = (VkBufferImageCopy){
            .bufferOffset = i * faceSize,
            .imageSubresource = { .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .mipLevel = 0, .baseArrayLayer = i, .layerCount = 1 },
            .imageExtent = { (uint32_t)width, (uint32_t)height, 1 }
        };
    }
    vkCmdCopyBufferToImage(cmd, stagingBuffer, out->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 6, regions);

    generate_mipmaps(ctx, cmd, out->image, out->width, out->height, out->mipLevels, 6);
    end_and_submit_upload_cmd(ctx, cmd);
    vmaDestroyBuffer(ctx->allocator, stagingBuffer, stagingAlloc);

    VkImageViewCreateInfo viewInfo = {
        .sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image    = out->image,
        .viewType = VK_IMAGE_VIEW_TYPE_CUBE,
        .format   = format,
        .subresourceRange = { .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .baseMipLevel = 0, .levelCount = mipLevels, .baseArrayLayer = 0, .layerCount = 6 }
    };
    result = vkCreateImageView(ctx->logicalDevice, &viewInfo, NULL, &out->imageView);
    if (result != VK_SUCCESS) {
        vmaDestroyImage(ctx->allocator, out->image, out->allocation);
        return (VulkanResult){.status = VULKAN_ERROR_IMAGE_VIEW_CREATION_FAILED, .vk_result = result};
    }
    LOG_INFO("Cubemap loaded: %s [and 5 other faces] (%ux%u, %u mip levels)", paths[0], out->width, out->height, mipLevels);
    return (VulkanResult){.status = VULKAN_SUCCESS, .vk_result = VK_SUCCESS};
}

VulkanResult vkTextureLoadCubemap(VkContext* ctx, const char* paths[6], VkTexture* out, uint32_t* outSlot) {
    VulkanResult result = vkTextureCreateCubemap(ctx, paths, out);
    if (result.status != VULKAN_SUCCESS) return result;
    *outSlot = vkTextureRegister(ctx, out, 3); 
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

VulkanResult vkTextureCreateCubemapPrecompiled(VkContext* ctx, const char* paths[][6], uint32_t mipLevels, VkTexture* out) {
    if (mipLevels == 0 || mipLevels > 16) {
        LOG_ERROR("Invalid cubemap mip level count specified: %u", mipLevels);
        return (VulkanResult){.status = VULKAN_ERROR_IMAGE_CREATION_FAILED, .vk_result = VK_ERROR_UNKNOWN};
    }

    int baseWidth = 0, baseHeight = 0, channels = 0;
    bool isHDR = stbi_is_hdr(paths[0][0]);
    VkFormat format = isHDR ? VK_FORMAT_R32G32B32A32_SFLOAT : VK_FORMAT_R8G8B8A8_SRGB;

    VkDeviceSize mipSizes[16];
    uint32_t mipWidths[16];
    uint32_t mipHeights[16];
    VkDeviceSize totalImageSize = 0;

    // Pass 1: Query image headers to validate dimensions and calculate staging buffer size
    for (uint32_t level = 0; level < mipLevels; level++) {
        int w, h, c;
        if (!stbi_info(paths[level][0], &w, &h, &c)) {
            LOG_ERROR("Failed to read image properties for mip level %u: %s", level, paths[level][0]);
            return (VulkanResult){.status = VULKAN_ERROR_IMAGE_CREATION_FAILED, .vk_result = VK_ERROR_UNKNOWN};
        }

        if (level == 0) {
            baseWidth = w;
            baseHeight = h;
        }

        mipWidths[level]  = (uint32_t)w;
        mipHeights[level] = (uint32_t)h;
        mipSizes[level]   = isHDR ? (VkDeviceSize)(w * h * 4 * sizeof(float)) : (VkDeviceSize)(w * h * 4);
        totalImageSize   += mipSizes[level] * 6;
    }

    // Allocate continuous staging buffer
    VkBuffer stagingBuffer;
    VmaAllocation stagingAlloc;
    VkBufferCreateInfo stagingBufInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size  = totalImageSize,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
    };
    VmaAllocationCreateInfo stagingAllocInfo = {
        .usage = VMA_MEMORY_USAGE_AUTO,
        .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
    };
    VmaAllocationInfo stagingAllocResult;
    VkResult vkResult = vmaCreateBuffer(ctx->allocator, &stagingBufInfo, &stagingAllocInfo, &stagingBuffer, &stagingAlloc, &stagingAllocResult);
    if (vkResult != VK_SUCCESS) {
        LOG_ERROR("Failed to create staging buffer for precompiled cubemap. VkResult: %i", vkResult);
        return (VulkanResult){.status = VULKAN_ERROR_BUFFER_CREATION_FAILED, .vk_result = vkResult};
    }

    uint8_t* pDst = (uint8_t*)stagingAllocResult.pMappedData;
    VkDeviceSize currentOffset = 0;
    uint32_t regionCount = 0;
    VkBufferImageCopy regions[6 * 16];

    // Pass 2: Load assets sequentially into staging memory and configure destination regions
    for (uint32_t face = 0; face < 6; face++) {
        for (uint32_t level = 0; level < mipLevels; level++) {
            const char* path = paths[level][face];
            int w, h, c;
            void* pixels = isHDR ? 
                stbi_loadf(path, &w, &h, &c, STBI_rgb_alpha) : 
                stbi_load(path, &w, &h, &c, STBI_rgb_alpha);

            if (!pixels) {
                LOG_ERROR("Failed to load cubemap asset: %s", path);
                vmaDestroyBuffer(ctx->allocator, stagingBuffer, stagingAlloc);
                return (VulkanResult){.status = VULKAN_ERROR_IMAGE_CREATION_FAILED, .vk_result = VK_ERROR_UNKNOWN};
            }

            if (w != (int)mipWidths[level] || h != (int)mipHeights[level]) {
                LOG_ERROR("Dimension mismatch at face %u, level %u! Expected %ux%u, got %ux%u", face, level, mipWidths[level], mipHeights[level], w, h);
                stbi_image_free(pixels);
                vmaDestroyBuffer(ctx->allocator, stagingBuffer, stagingAlloc);
                return (VulkanResult){.status = VULKAN_ERROR_IMAGE_CREATION_FAILED, .vk_result = VK_ERROR_UNKNOWN};
            }

            memcpy(pDst + currentOffset, pixels, (size_t)mipSizes[level]);
            stbi_image_free(pixels);

            regions[regionCount++] = (VkBufferImageCopy){
                .bufferOffset      = currentOffset,
                .bufferRowLength   = 0,
                .bufferImageHeight = 0,
                .imageSubresource  = {
                    .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                    .mipLevel       = level,
                    .baseArrayLayer = face,
                    .layerCount     = 1,
                },
                .imageOffset = { 0, 0, 0 },
                .imageExtent = { (uint32_t)w, (uint32_t)h, 1 },
            };

            currentOffset += mipSizes[level];
        }
    }

    // Create the GPU Image setup for a Cubemap with explicitly loaded mip levels
    VkImageCreateInfo imageInfo = {
        .sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .flags         = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT,
        .imageType     = VK_IMAGE_TYPE_2D,
        .format        = format,
        .extent        = { (uint32_t)baseWidth, (uint32_t)baseHeight, 1 },
        .mipLevels     = mipLevels,
        .arrayLayers   = 6,
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

    vkResult = vmaCreateImage(ctx->allocator, &imageInfo, &imageAllocInfo, &out->image, &out->allocation, NULL);
    if (vkResult != VK_SUCCESS) {
        vmaDestroyBuffer(ctx->allocator, stagingBuffer, stagingAlloc);
        LOG_ERROR("vmaCreateImage failed for precompiled cubemap. VkResult: %i", vkResult);
        return (VulkanResult){.status = VULKAN_ERROR_IMAGE_CREATION_FAILED, .vk_result = vkResult};
    }

    out->width     = (uint32_t)baseWidth;
    out->height    = (uint32_t)baseHeight;
    out->mipLevels = mipLevels;

    VkCommandBuffer cmd;
    VulkanResult vr = begin_upload_cmd(ctx, &cmd);
    if (vr.status != VULKAN_SUCCESS) {
        vmaDestroyBuffer(ctx->allocator, stagingBuffer, stagingAlloc);
        vmaDestroyImage(ctx->allocator, out->image, out->allocation);
        return vr;
    }

    // Transition image to transfer layout
    transition_image_layout(ctx, cmd, out->image,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_PIPELINE_STAGE_2_NONE, 0,
        VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
        0, mipLevels, 6);

    // Copy all layers and mip map faces in one go
    vkCmdCopyBufferToImage(cmd, stagingBuffer, out->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, regionCount, regions);

    // Transition to shader read layout
    transition_image_layout(ctx, cmd, out->image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT,
        0, mipLevels, 6);

    vr = end_and_submit_upload_cmd(ctx, cmd);
    vmaDestroyBuffer(ctx->allocator, stagingBuffer, stagingAlloc);

    if (vr.status != VULKAN_SUCCESS) {
        vmaDestroyImage(ctx->allocator, out->image, out->allocation);
        return vr;
    }

    // Create the image view configured as a cubemap
    VkImageViewCreateInfo viewInfo = {
        .sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image    = out->image,
        .viewType = VK_IMAGE_VIEW_TYPE_CUBE,
        .format   = format,
        .subresourceRange = {
            .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel   = 0,
            .levelCount     = mipLevels,
            .baseArrayLayer = 0,
            .layerCount     = 6,
        }
    };

    vkResult = vkCreateImageView(ctx->logicalDevice, &viewInfo, NULL, &out->imageView);
    if (vkResult != VK_SUCCESS) {
        vmaDestroyImage(ctx->allocator, out->image, out->allocation);
        LOG_ERROR("vkCreateImageView failed for precompiled cubemap. VkResult: %i", vkResult);
        return (VulkanResult){.status = VULKAN_ERROR_IMAGE_VIEW_CREATION_FAILED, .vk_result = vkResult};
    }

    LOG_INFO("Precompiled cubemap loaded successfully: (%ux%u, %u mip levels)", out->width, out->height, mipLevels);
    return (VulkanResult){.status = VULKAN_SUCCESS, .vk_result = VK_SUCCESS};
}

VulkanResult vkTextureLoadCubemapPrecompiled(VkContext* ctx, const char* paths[][6], uint32_t mipLevels, VkTexture* out, uint32_t* outSlot) {
    VulkanResult result = vkTextureCreateCubemapPrecompiled(ctx, paths, mipLevels, out);
    if (result.status != VULKAN_SUCCESS) return result;
    *outSlot = vkTextureRegister(ctx, out, 3);
    return (VulkanResult){.status = VULKAN_SUCCESS, .vk_result = VK_SUCCESS};
}