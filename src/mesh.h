#ifndef MESH_H
#define MESH_H

#include "cglm/cglm.h"
#include "vulkan_ctx.h"
#include "buffer.h"

typedef struct Vertex {
    vec3 position;
    vec3 color;
    vec3 normal;
    vec2 uv;
    vec4 tangent;
} Vertex;

typedef struct VkMeshCreateInfo {
    Vertex*   vertexArray;
    uint32_t  vertexCount;
    uint32_t* indexArray;
    uint32_t  indexCount;
} VkMeshCreateInfo;

typedef struct VkMesh {
    VulkanBuffer buffer;
    uint32_t     vertexCount;
    uint32_t     indexCount;
    VkDeviceSize indexOffset;

    vec3 local_min;
    vec3 local_max;
} VkMesh;

VkVertexInputBindingDescription vkVertexGetBindingDescription(void);
void vkVertexGetAttributeDescription(VkVertexInputAttributeDescription attributes[5]);

VulkanResult vkMeshCreate(VkContext* ctx, VkMeshCreateInfo* createInfo, VkMesh* outMesh);
void         vkMeshBind  (VkMesh* mesh, VkCommandBuffer cmd);
void         vkMeshDraw  (VkMesh* mesh, VkCommandBuffer cmd, uint32_t firstInstance);
void         vkMeshDestroy(VkContext* ctx, VkMesh* mesh);

#endif