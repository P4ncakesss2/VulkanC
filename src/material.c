#include "material.h"
#include "mesh.h"
#include "logger.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static char* read_spv(const char* path, size_t* outSize) {
    FILE* f = fopen(path, "rb");
    if (!f) { LOG_ERROR("Failed to open: %s", path); return NULL; }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* buf = malloc((size_t)sz);
    if (!buf) { fclose(f); return NULL; }
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) { free(buf); fclose(f); return NULL; }
    fclose(f);
    *outSize = (size_t)sz;
    return buf;
}

static MaterialParamType infer_type(const SpvReflectBlockVariable* m) {
    switch (m->size) {
        case  4: return (m->type_description &&
                         m->type_description->traits.numeric.scalar.signedness)
                         ? MATERIAL_PARAM_INT : MATERIAL_PARAM_FLOAT;
        case  8: return MATERIAL_PARAM_FLOAT2;
        case 12: return MATERIAL_PARAM_FLOAT3;
        case 16: return MATERIAL_PARAM_FLOAT4;
        case 64: return MATERIAL_PARAM_MAT4;
        default: return MATERIAL_PARAM_FLOAT;
    }
}

void vkMaterialInit(VkMaterial* mat, const VkPipelineBuilder* builder) {
    memset(mat, 0, sizeof(*mat));
    mat->builder = *builder;

    for (uint32_t si = 0; si < builder->stageCount; si++) {
        size_t spvSize = 0;
        char* spv = read_spv(builder->stages[si].path, &spvSize);
        if (!spv) continue;

        SpvReflectShaderModule mod;
        if (spvReflectCreateShaderModule(spvSize, spv, &mod) != SPV_REFLECT_RESULT_SUCCESS) {
            free(spv); continue;
        }
        free(spv);

        uint32_t setCount = 0;
        spvReflectEnumerateDescriptorSets(&mod, &setCount, NULL);
        SpvReflectDescriptorSet** sets = malloc(setCount * sizeof(*sets));
        spvReflectEnumerateDescriptorSets(&mod, &setCount, sets);

        for (uint32_t di = 0; di < setCount; di++) {
            if (sets[di]->set != 2) continue;
            SpvReflectDescriptorSet* s = sets[di];
            for (uint32_t bi = 0; bi < s->binding_count; bi++) {
                SpvReflectDescriptorBinding* b = s->bindings[bi];
                if (b->descriptor_type != SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER) continue;
                if (b->block.size > sizeof(mat->uboParams.data)) continue;
                mat->uboParams.binding = b->binding;
                mat->uboParams.size    = b->block.size;
                for (uint32_t mi = 0; mi < b->block.member_count && mat->paramCount < MATERIAL_MAX_PARAMS; mi++) {
                    SpvReflectBlockVariable* m = &b->block.members[mi];
                    MaterialParamEntry* e = &mat->params[mat->paramCount++];
                    strncpy(e->name, m->name, MATERIAL_NAME_MAX - 1);
                    e->binding = b->binding;
                    e->offset  = m->offset;
                    e->size    = m->size;
                    SpvReflectBlockVariable tmp = { .size = m->size, .type_description = m->type_description };
                    e->type = infer_type(&tmp);
                }
            }
        }
        free(sets);
        spvReflectDestroyShaderModule(&mod);
    }
}

bool vkMaterialSetParam(VkMaterial* mat, const char* name, MaterialParamValue value) {
    for (uint32_t i = 0; i < mat->paramCount; i++) {
        MaterialParamEntry* e = &mat->params[i];
        if (strncmp(e->name, name, MATERIAL_NAME_MAX) != 0) continue;
        void* dst = mat->uboParams.data + e->offset;
        switch (value.type) {
            case MATERIAL_PARAM_FLOAT:  memcpy(dst, &value.f,   4);  break;
            case MATERIAL_PARAM_FLOAT2: memcpy(dst, value.f2,   8);  break;
            case MATERIAL_PARAM_FLOAT3: memcpy(dst, value.f3,  12);  break;
            case MATERIAL_PARAM_FLOAT4: memcpy(dst, value.f4,  16);  break;
            case MATERIAL_PARAM_INT:    memcpy(dst, &value.i,   4);  break;
            case MATERIAL_PARAM_UINT:   memcpy(dst, &value.u,   4);  break;
            case MATERIAL_PARAM_MAT4:   memcpy(dst, value.mat4, 64); break;
            default: return false;
        }
        return true;
    }
    LOG_WARN("vkMaterialSetParam: no param '%s'", name);
    return false;
}

void vkMaterialSetParams(VkMaterial* mat, const void* src,
                         const MaterialFieldDesc* fields, uint32_t count)
{
    for (uint32_t i = 0; i < count; i++) {
        const MaterialFieldDesc* f = &fields[i];
        MaterialParamValue v = { .type = f->type };
        const void* s = (const uint8_t*)src + f->offset;
        switch (f->type) {
            case MATERIAL_PARAM_FLOAT:  memcpy(&v.f,   s, 4);  break;
            case MATERIAL_PARAM_FLOAT2: memcpy(v.f2,   s, 8);  break;
            case MATERIAL_PARAM_FLOAT3: memcpy(v.f3,   s, 12); break;
            case MATERIAL_PARAM_FLOAT4: memcpy(v.f4,   s, 16); break;
            case MATERIAL_PARAM_INT:    memcpy(&v.i,   s, 4);  break;
            case MATERIAL_PARAM_UINT:   memcpy(&v.u,   s, 4);  break;
            case MATERIAL_PARAM_MAT4:   memcpy(v.mat4, s, 64); break;
            default: break;
        }
        vkMaterialSetParam(mat, f->name, v);
    }
}

VkPipelineBuilder vkPipelineBuilderCreateDefault(void) {
    VkPipelineBuilder b = {0};
    b.topology               = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    b.primitiveRestartEnable = VK_FALSE;
    b.polygonMode            = VK_POLYGON_MODE_FILL;
    b.cullMode               = VK_CULL_MODE_BACK_BIT;
    b.frontFace              = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    b.lineWidth              = 1.0f;
    b.msaaSamples            = VK_SAMPLE_COUNT_1_BIT;
    b.sampleShadingEnable    = VK_FALSE;
    b.minSampleShading       = 1.0f;
    b.depthTestEnable        = VK_TRUE;
    b.depthWriteEnable       = VK_TRUE;
    b.depthCompareOp         = VK_COMPARE_OP_LESS;
    b.colorBlendAttachment.blendEnable    = VK_FALSE;
    b.colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                                            VK_COLOR_COMPONENT_G_BIT |
                                            VK_COLOR_COMPONENT_B_BIT |
                                            VK_COLOR_COMPONENT_A_BIT;
    b.vertexBindingCount   = 1;
    b.vertexBindings[0]    = vkVertexGetBindingDescription();
    b.vertexAttributeCount = 4;
    vkVertexGetAttributeDescription(b.vertexAttributes);
    return b;
}