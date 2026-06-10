#ifndef MESH_H
#define MESH_H

#include "cglm/cglm.h"
#include "vulkan_ctx.h"

typedef struct Vertex {
    vec3 position;
    vec3 color;
} Vertex;

typedef struct VkMeshCreateInfo {
    Vertex* vertexArray;
    uint32_t vertexCount;
    uint32_t* indexArray;
    uint32_t indexCount;
} VkMeshCreateInfo;

typedef struct VkMesh {
    VkBuffer      buffer;
    VmaAllocation allocation;
    uint32_t      vertexCount;
    uint32_t      indexCount;
    VkDeviceSize  indexOffset;
} VkMesh;

VkVertexInputBindingDescription vkVertexGetBindingDescription();
void vkVertexGetAttributeDescription(VkVertexInputAttributeDescription attributes[2]);

VulkanResult vkMeshCreate(VkContext* ctx, VkMeshCreateInfo* createInfo, VkMesh* outMesh);
VulkanResult vkMeshBind(VkMesh* mesh, VkCommandBuffer cmd);
void vkMeshDestroy(VkContext* ctx, VkMesh* mesh);

#endif