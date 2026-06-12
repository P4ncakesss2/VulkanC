#ifndef RENDER_TYPES_H
#define RENDER_TYPES_H

#include "cglm/cglm.h"

typedef struct GlobalUBO {
    mat4  view;
    mat4  proj;
    mat4 invProj;
    float time;
} GlobalUBO;

#define MAX_OBJECTS 128

typedef struct {
    mat4     transform;
    uint32_t textureID;
    uint32_t pad[3];
} ObjectSSBO;

#endif