#ifndef TEXTURE_H
#define TEXTURE_H

#include "vulkan_ctx.h"
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <stdint.h>

#define MAX_TEXTURES 128
#define MAX_MIP_LEVELS 16

typedef struct {
    VkImage       image;
    VkImageView   imageView;
    VmaAllocation allocation;
    uint32_t      width;
    uint32_t      height;
    uint32_t      mipLevels;
} VkTexture;

VulkanResult vkTextureCreate(VkContext* ctx, const char* path, VkTexture* out, bool IsLinear);
uint32_t vkTextureRegister(VkContext* ctx, VkTexture* tex, uint32_t binding);
VulkanResult vkTextureLoad(VkContext* ctx, const char* path, VkTexture* out, uint32_t* outSlot, bool IsLinear);
VulkanResult vkTextureCreateCubemap(VkContext* ctx, const char* paths[6], VkTexture* out);
VulkanResult vkTextureLoadCubemap(VkContext* ctx, const char* paths[6], VkTexture* out, uint32_t* outSlot);

VulkanResult vkTextureCreateCubemapPrecompiled(VkContext* ctx, const char* paths[][6], uint32_t mipLevels, VkTexture* out);
VulkanResult vkTextureLoadCubemapPrecompiled(VkContext* ctx, const char* paths[][6], uint32_t mipLevels, VkTexture* out, uint32_t* outSlot);

void vkTextureDestroy(VkContext* ctx, VkTexture* tex);
void vkTextureRegisterGlobalSampler(VkContext* ctx);

#endif