#ifndef RENDER_TYPES_H
#define RENDER_TYPES_H

#include "cglm/cglm.h"

typedef struct GlobalUBO {
    mat4  view;
    mat4  proj;
    float time;
    float _pad[3];
} GlobalUBO;

#define MAX_OBJECTS 1024

typedef struct ObjectSSBO {
    mat4 modelMatrices[MAX_OBJECTS];
} ObjectSSBO;

#endif