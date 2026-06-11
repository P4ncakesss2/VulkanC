#ifndef MESH_H
#define MESH_H

#include "cglm/cglm.h"
#include "vulkan_ctx.h"

typedef struct Vertex {
    vec3  position;
    vec3  color;
    vec2  uv;
} Vertex;

typedef struct VkMeshCreateInfo {
    Vertex*   vertexArray;
    uint32_t  vertexCount;
    uint32_t* indexArray;
    uint32_t  indexCount;
    uint32_t  textureID;
} VkMeshCreateInfo;

typedef struct VkMesh {
    VkBuffer      buffer;
    VmaAllocation allocation;
    uint32_t      vertexCount;
    uint32_t      indexCount;
    VkDeviceSize  indexOffset;
    uint32_t      textureID;
} VkMesh;

VkVertexInputBindingDescription vkVertexGetBindingDescription();
void vkVertexGetAttributeDescription(VkVertexInputAttributeDescription attributes[3]);

VulkanResult vkMeshCreate(VkContext* ctx, VkMeshCreateInfo* createInfo, VkMesh* outMesh);
void vkMeshDraw(VkMesh* mesh, VkCommandBuffer cmd, uint32_t firstInstance);
void vkMeshDestroy(VkContext* ctx, VkMesh* mesh);

#endif