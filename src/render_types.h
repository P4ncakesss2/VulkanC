#ifndef RENDER_TYPES_H
#define RENDER_TYPES_H

#include "cglm/cglm.h"
#include "mesh.h"
#include "material.h"

typedef struct GlobalUBO {
    float time;
} GlobalUBO;

#define MAX_OBJECTS 1024

typedef struct ObjectSSBO {
    mat4  transform;
    vec3  local_min;
    float pad0;
    vec3  local_max; 
    uint indexCount;
    uint firstIndex;
    int vertexOffset;
    uint pad1;
} ObjectSSBO;

typedef struct RenderObject {
    VkMesh* mesh;
    VkMaterial* material;
    mat4        transform;
    uint32_t    textureID;
} RenderObject;

#endif