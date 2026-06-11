#ifndef TEXTURE_H
#define TEXTURE_H

#include "vulkan_ctx.h"
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <stdint.h>

#define MAX_TEXTURES 1024

typedef struct {
    VkImage       image;
    VkImageView   imageView;
    VmaAllocation allocation;
    uint32_t      width;
    uint32_t      height;
} VkTexture;

VulkanResult vkTextureCreate(VkContext* ctx, const char* path, VkTexture* out);
uint32_t vkTextureRegister(VkContext* ctx, VkTexture* tex);
VulkanResult vkTextureLoad(VkContext* ctx, const char* path, VkTexture* out, uint32_t* outSlot);

void vkTextureDestroy(VkContext* ctx, VkTexture* tex);
void vkTextureRegisterGlobalSampler(VkContext* ctx);

#endif