#include "material.h"
#include "mesh.h"
#include "logger.h"
#include "buffer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

static char* read_file(const char* path, size_t* outSize) {
    FILE* f = fopen(path, "rb");
    if (!f) { LOG_ERROR("Failed to open: %s", path); return NULL; }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    if (sz < 0) { LOG_ERROR("ftell failed: %s", path); fclose(f); return NULL; }
    fseek(f, 0, SEEK_SET);
    char* buf = malloc((size_t)sz);
    if (!buf) { LOG_ERROR("OOM reading: %s", path); fclose(f); return NULL; }
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        LOG_ERROR("Short read: %s", path); free(buf); fclose(f); return NULL;
    }
    fclose(f);
    *outSize = (size_t)sz;
    return buf;
}

static VkShaderModule create_shader_module(VkDevice device, const char* code, size_t size) {
    VkShaderModuleCreateInfo ci = {
        .sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = size,
        .pCode    = (const uint32_t*)code,
    };
    VkShaderModule m;
    if (vkCreateShaderModule(device, &ci, NULL, &m) != VK_SUCCESS) {
        LOG_ERROR("Failed to create shader module");
        return VK_NULL_HANDLE;
    }
    return m;
}

static MaterialParamType infer_ubo_member_type(const SpvReflectBlockVariable* m) {
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

// ---------------------------------------------------------------------------
// vkMaterialBuildDescriptorSet
// Reflects set 2 from all shader stages and builds the unified per-material pool.
// ---------------------------------------------------------------------------
VulkanResult vkMaterialBuildDescriptorSet(VkContext* ctx,
                                           VkPipelineBuilder* builder,
                                           VkMaterial* mat)
{
    typedef struct {
        uint32_t               binding;
        VkDescriptorType       vkType;
        uint32_t               count;
        VkShaderStageFlags     stages;
        uint32_t               blockSize;
        uint32_t               memberCount;
        struct {
            char     name[MATERIAL_NAME_MAX];
            uint32_t offset;
            uint32_t size;
            SpvReflectTypeDescription* type_description;
        } members[MATERIAL_MAX_PARAMS];
    } BindingInfo;

    BindingInfo bindings[MATERIAL_MAX_BINDINGS];
    uint32_t    bindingCount = 0;

    for (uint32_t si = 0; si < builder->stageCount; si++) {
        size_t spvSize = 0;
        char* spv = read_file(builder->stages[si].path, &spvSize);
        if (!spv) continue;

        SpvReflectShaderModule module;
        if (spvReflectCreateShaderModule(spvSize, spv, &module) != SPV_REFLECT_RESULT_SUCCESS) {
            free(spv); continue;
        }
        free(spv);

        uint32_t setCount = 0;
        spvReflectEnumerateDescriptorSets(&module, &setCount, NULL);
        SpvReflectDescriptorSet** sets = malloc(setCount * sizeof(*sets));
        spvReflectEnumerateDescriptorSets(&module, &setCount, sets);

        for (uint32_t di = 0; di < setCount; di++) {
            if (sets[di]->set != 2) continue;  // only the per-material set
            SpvReflectDescriptorSet* s = sets[di];

            VkShaderStageFlags stageFlag =
                (builder->stages[si].stage == VK_SHADER_STAGE_VERTEX_BIT)   ? VK_SHADER_STAGE_VERTEX_BIT :
                (builder->stages[si].stage == VK_SHADER_STAGE_FRAGMENT_BIT) ? VK_SHADER_STAGE_FRAGMENT_BIT :
                (builder->stages[si].stage == VK_SHADER_STAGE_COMPUTE_BIT)  ? VK_SHADER_STAGE_COMPUTE_BIT :
                VK_SHADER_STAGE_ALL_GRAPHICS;

            for (uint32_t bi = 0; bi < s->binding_count; bi++) {
                SpvReflectDescriptorBinding* b = s->bindings[bi];
                if (b->descriptor_type != SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
                    continue;

                BindingInfo* slot = NULL;
                for (uint32_t k = 0; k < bindingCount; k++) {
                    if (bindings[k].binding == b->binding) { slot = &bindings[k]; break; }
                }
                if (!slot) {
                    if (bindingCount >= MATERIAL_MAX_BINDINGS) continue;
                    slot = &bindings[bindingCount++];
                    memset(slot, 0, sizeof(*slot));
                    slot->binding   = b->binding;
                    slot->vkType    = (VkDescriptorType)b->descriptor_type;
                    slot->count     = b->count;
                    slot->blockSize = b->block.size;

                    slot->memberCount = b->block.member_count < MATERIAL_MAX_PARAMS
                                        ? b->block.member_count : MATERIAL_MAX_PARAMS;
                    for (uint32_t mi = 0; mi < slot->memberCount; mi++) {
                        SpvReflectBlockVariable* m = &b->block.members[mi];
                        strncpy(slot->members[mi].name, m->name, MATERIAL_NAME_MAX - 1);
                        slot->members[mi].name[MATERIAL_NAME_MAX - 1] = '\0';
                        slot->members[mi].offset           = m->offset;
                        slot->members[mi].size             = m->size;
                        slot->members[mi].type_description = m->type_description;
                    }
                }
                slot->stages |= stageFlag;
            }
        }

        free(sets);
        spvReflectDestroyShaderModule(&module);
    }

    if (bindingCount == 0) {
        mat->hasSet = false;
        return (VulkanResult){.status = VULKAN_SUCCESS, .vk_result = VK_SUCCESS};
    }

    VkDescriptorSetLayoutBinding vkBindings[MATERIAL_MAX_BINDINGS];
    for (uint32_t i = 0; i < bindingCount; i++) {
        vkBindings[i] = (VkDescriptorSetLayoutBinding){
            .binding         = bindings[i].binding,
            .descriptorType  = bindings[i].vkType,
            .descriptorCount = bindings[i].count,
            .stageFlags      = bindings[i].stages,
        };
    }
    VkDescriptorSetLayoutCreateInfo layoutCI = {
        .sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = bindingCount,
        .pBindings    = vkBindings,
    };
    VkResult r = vkCreateDescriptorSetLayout(ctx->logicalDevice, &layoutCI, NULL, &mat->setLayout);
    if (r != VK_SUCCESS) {
        LOG_ERROR("Failed to create material set layout");
        return (VulkanResult){.status = VULKAN_ERROR_DESCRIPTOR_SET_LAYOUT_CREATION_FAILED, .vk_result = r};
    }

    VkDescriptorPoolSize poolSizes[MATERIAL_MAX_BINDINGS];
    uint32_t poolSizeCount = 0;
    for (uint32_t i = 0; i < bindingCount; i++) {
        bool found = false;
        for (uint32_t p = 0; p < poolSizeCount; p++) {
            if (poolSizes[p].type == bindings[i].vkType) {
                poolSizes[p].descriptorCount += bindings[i].count * MAX_FRAMES_IN_FLIGHT;
                found = true; break;
            }
        }
        if (!found) poolSizes[poolSizeCount++] = (VkDescriptorPoolSize){
            .type            = bindings[i].vkType,
            .descriptorCount = bindings[i].count * MAX_FRAMES_IN_FLIGHT,
        };
    }
    VkDescriptorPoolCreateInfo poolCI = {
        .sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets       = MAX_FRAMES_IN_FLIGHT,
        .poolSizeCount = poolSizeCount,
        .pPoolSizes    = poolSizes,
    };
    r = vkCreateDescriptorPool(ctx->logicalDevice, &poolCI, NULL, &mat->pool);
    if (r != VK_SUCCESS) {
        vkDestroyDescriptorSetLayout(ctx->logicalDevice, mat->setLayout, NULL);
        LOG_ERROR("Failed to create material descriptor pool");
        return (VulkanResult){.status = VULKAN_ERROR_DESCRIPTOR_POOL_CREATION_FAILED, .vk_result = r};
    }

    VkDescriptorSetLayout frameLayouts[MAX_FRAMES_IN_FLIGHT];
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) frameLayouts[i] = mat->setLayout;
    VkDescriptorSetAllocateInfo dsAI = {
        .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool     = mat->pool,
        .descriptorSetCount = MAX_FRAMES_IN_FLIGHT,
        .pSetLayouts        = frameLayouts,
    };
    r = vkAllocateDescriptorSets(ctx->logicalDevice, &dsAI, mat->sets);
    if (r != VK_SUCCESS) {
        vkDestroyDescriptorPool(ctx->logicalDevice, mat->pool, NULL);
        vkDestroyDescriptorSetLayout(ctx->logicalDevice, mat->setLayout, NULL);
        return (VulkanResult){.status = VULKAN_ERROR_DESCRIPTOR_SET_ALLOCATION_FAILED, .vk_result = r};
    }

    for (uint32_t i = 0; i < bindingCount; i++) {
        BindingInfo* b = &bindings[i];
        if (mat->uboCount >= MATERIAL_MAX_BINDINGS) continue;
        MaterialUBOBuffer* ubo = &mat->uboBuffers[mat->uboCount++];
        ubo->binding   = b->binding;
        ubo->totalSize = b->blockSize;
        ubo->data      = calloc(1, b->blockSize);

        for (uint32_t f = 0; f < MAX_FRAMES_IN_FLIGHT; f++) {
            VulkanResult vr = vkBufferCreate(ctx, b->blockSize,
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                VMA_ALLOCATION_CREATE_MAPPED_BIT,
                &ubo->gpuBuffers[f]);
            if (vr.status != VULKAN_SUCCESS) {
                for (uint32_t ff = 0; ff < f; ff++)
                    vkBufferDestroy(ctx, &ubo->gpuBuffers[ff]);
                free(ubo->data);
                mat->uboCount--;
                LOG_WARN("Failed to allocate UBO gpu buffer for binding %u", b->binding);
                break;
            }
        }

        for (uint32_t f = 0; f < MAX_FRAMES_IN_FLIGHT; f++) {
            VkDescriptorBufferInfo bufInfo = {
                .buffer = ubo->gpuBuffers[f].buffer,
                .offset = 0,
                .range  = ubo->totalSize,
            };
            VkWriteDescriptorSet w = {
                .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet          = mat->sets[f],
                .dstBinding      = b->binding,
                .descriptorCount = 1,
                .descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .pBufferInfo     = &bufInfo,
            };
            vkUpdateDescriptorSets(ctx->logicalDevice, 1, &w, 0, NULL);
        }

        for (uint32_t mi = 0; mi < b->memberCount; mi++) {
            if (mat->paramCount >= MATERIAL_MAX_PARAMS) break;
            MaterialParamEntry* e = &mat->params[mat->paramCount++];
            strncpy(e->name, b->members[mi].name, MATERIAL_NAME_MAX - 1);
            e->name[MATERIAL_NAME_MAX - 1] = '\0';
            e->binding = b->binding;
            e->offset  = b->members[mi].offset;
            e->size    = b->members[mi].size;
            SpvReflectBlockVariable tmp = { .size = b->members[mi].size,
                                            .type_description = b->members[mi].type_description };
            e->type = infer_ubo_member_type(&tmp);
        }
    }

    mat->hasSet = true;
    return (VulkanResult){.status = VULKAN_SUCCESS, .vk_result = VK_SUCCESS};
}

VulkanResult vkMaterialBuildForPass(VkContext* ctx,
                                     VkRenderer* renderer,
                                     VkPipelineBuilder* builder,
                                     VkPassType passType,
                                     VkMaterial* mat)
{
    if (mat->pipelines[passType] != VK_NULL_HANDLE) {
        LOG_WARN("vkMaterialBuildForPass: passType %d already compiled, skipping", passType);
        return (VulkanResult){.status = VULKAN_SUCCESS, .vk_result = VK_SUCCESS};
    }

    uint32_t activeStageCount = 0;
    for (uint32_t i = 0; i < builder->stageCount; i++) {
        if (builder->stages[i].pass == passType) {
            activeStageCount++;
        }
    }

    if (activeStageCount == 0) {
        return (VulkanResult){.status = VULKAN_SUCCESS, .vk_result = VK_SUCCESS};
    }

    VkPipelineShaderStageCreateInfo* shaderStages = malloc(sizeof(VkPipelineShaderStageCreateInfo) * activeStageCount);
    VkShaderModule* shaderModules = malloc(sizeof(VkShaderModule) * activeStageCount);

    uint32_t compiledIdx = 0;
    bool isCompute = false;

    for (uint32_t i = 0; i < builder->stageCount; i++) {
        if (builder->stages[i].pass != passType) continue;

        size_t sz = 0;
        char* code = read_file(builder->stages[i].path, &sz);
        if (!code) {
            for (uint32_t j = 0; j < compiledIdx; j++)
                vkDestroyShaderModule(ctx->logicalDevice, shaderModules[j], NULL);
            free(shaderStages); free(shaderModules);
            return (VulkanResult){.status = VULKAN_ERROR_FILE_READ_FAILED, .vk_result = VK_ERROR_UNKNOWN};
        }
        
        shaderModules[compiledIdx] = create_shader_module(ctx->logicalDevice, code, sz);
        free(code);

        shaderStages[compiledIdx] = (VkPipelineShaderStageCreateInfo){
            .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage  = builder->stages[i].stage,
            .module = shaderModules[compiledIdx],
            .pName  = builder->stages[i].entryName,
        };

        if (builder->stages[i].stage == VK_SHADER_STAGE_COMPUTE_BIT) {
            isCompute = true;
        }

        compiledIdx++;
    }

    // Build pipeline layout: set0=global, set1=pass-specific, set2=per-material (optional)
    VkDescriptorSetLayout layouts[3] = {
        ctx->globalSetLayout,
        renderer->setLayouts[passType],
    };
    uint32_t layoutCount = 2;
    if (mat->hasSet) layouts[layoutCount++] = mat->setLayout;

    VkPipelineLayoutCreateInfo pipelineLayoutCI = {
        .sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = layoutCount,
        .pSetLayouts    = layouts,
    };
    VkResult r = vkCreatePipelineLayout(ctx->logicalDevice, &pipelineLayoutCI,
                                         NULL, &mat->pipelineLayouts[passType]);
    if (r != VK_SUCCESS) {
        for (uint32_t i = 0; i < activeStageCount; i++)
            vkDestroyShaderModule(ctx->logicalDevice, shaderModules[i], NULL);
        free(shaderStages); free(shaderModules);
        return (VulkanResult){.status = VULKAN_ERROR_PIPELINE_LAYOUT_CREATION_FAILED, .vk_result = r};
    }

    VkResult pipelineResult;
    if (isCompute) {
        VkComputePipelineCreateInfo ci = {
            .sType  = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
            .layout = mat->pipelineLayouts[passType],
            .stage  = shaderStages[0],
        };
        pipelineResult = vkCreateComputePipelines(ctx->logicalDevice, VK_NULL_HANDLE,
                                                   1, &ci, NULL, &mat->pipelines[passType]);
    } else {
        VkPipelineVertexInputStateCreateInfo vertexInput = {
            .sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
            .vertexBindingDescriptionCount              = builder->vertexBindingCount,
            .pVertexBindingDescriptions      = builder->vertexBindingCount > 0 ? builder->vertexBindings : NULL,
            .vertexAttributeDescriptionCount = builder->vertexAttributeCount,
            .pVertexAttributeDescriptions    = builder->vertexAttributeCount > 0 ? builder->vertexAttributes : NULL,
        };
        VkPipelineInputAssemblyStateCreateInfo inputAssembly = {
            .sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
            .topology               = builder->topology,
            .primitiveRestartEnable = builder->primitiveRestartEnable,
        };
        VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo dynamicState = {
            .sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
            .dynamicStateCount = 2,
            .pDynamicStates    = dynamicStates,
        };
        VkPipelineViewportStateCreateInfo viewportState = {
            .sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
            .viewportCount = 1,
            .scissorCount  = 1,
        };
        VkPipelineRasterizationStateCreateInfo rasterizer = {
            .sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
            .polygonMode = builder->polygonMode,
            .cullMode    = builder->cullMode,
            .frontFace   = builder->frontFace,
            .lineWidth   = builder->lineWidth,
        };
        VkPipelineMultisampleStateCreateInfo multisampling = {
            .sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
            .rasterizationSamples = builder->msaaSamples,
            .sampleShadingEnable  = builder->sampleShadingEnable,
            .minSampleShading     = builder->minSampleShading,
        };
        VkPipelineColorBlendStateCreateInfo colorBlending = {
            .sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
            .attachmentCount = 1,
            .pAttachments    = &builder->colorBlendAttachment,
        };
        VkPipelineDepthStencilStateCreateInfo depthStencil = {
            .sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
            .depthTestEnable  = builder->depthTestEnable,
            .depthWriteEnable = builder->depthWriteEnable,
            .depthCompareOp   = builder->depthCompareOp,
        };
        VkPipelineRenderingCreateInfo renderingCI = {
            .sType                   = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
            .colorAttachmentCount    = 1,
            .pColorAttachmentFormats = &builder->colorFormat,
            .depthAttachmentFormat   = builder->depthFormat,
        };
        VkGraphicsPipelineCreateInfo graphicsCI = {
            .sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
            .pNext               = &renderingCI,
            .stageCount          = activeStageCount,
            .pStages             = shaderStages,
            .pVertexInputState   = &vertexInput,
            .pInputAssemblyState = &inputAssembly,
            .pDepthStencilState  = &depthStencil,
            .pViewportState      = &viewportState,
            .pRasterizationState = &rasterizer,
            .pMultisampleState   = &multisampling,
            .pColorBlendState    = &colorBlending,
            .pDynamicState       = &dynamicState,
            .layout              = mat->pipelineLayouts[passType],
        };
        pipelineResult = vkCreateGraphicsPipelines(ctx->logicalDevice, VK_NULL_HANDLE,
                                                    1, &graphicsCI, NULL,
                                                    &mat->pipelines[passType]);
    }

    for (uint32_t i = 0; i < activeStageCount; i++)
        vkDestroyShaderModule(ctx->logicalDevice, shaderModules[i], NULL);
    free(shaderStages);
    free(shaderModules);

    if (pipelineResult != VK_SUCCESS) {
        vkDestroyPipelineLayout(ctx->logicalDevice, mat->pipelineLayouts[passType], NULL);
        mat->pipelineLayouts[passType] = VK_NULL_HANDLE;
        return (VulkanResult){.status = VULKAN_ERROR_PIPELINE_CREATION_FAILED,
                              .vk_result = pipelineResult};
    }

    mat->isCompute = isCompute;
    return (VulkanResult){.status = VULKAN_SUCCESS, .vk_result = VK_SUCCESS};
}

VulkanResult vkMaterialBuild(VkContext* ctx,
                              VkRenderer* renderer,
                              VkPipelineBuilder* builder,
                              VkMaterial* out)
{
    memset(out, 0, sizeof(*out));

    VulkanResult r = vkMaterialBuildDescriptorSet(ctx, builder, out);
    if (r.status != VULKAN_SUCCESS) return r;

    bool activePasses[PASS_TYPE_COUNT] = {false};
    for (uint32_t i = 0; i < builder->stageCount; i++) {
        VkPassType pass = builder->stages[i].pass;
        if ((uint32_t)pass < PASS_TYPE_COUNT) {
            activePasses[pass] = true;
        }
    }

    for (uint32_t i = 0; i < PASS_TYPE_COUNT; i++) {
        if (!activePasses[i]) continue;
        
        r = vkMaterialBuildForPass(ctx, renderer, builder, (VkPassType)i, out);
        if (r.status != VULKAN_SUCCESS) {
            vkMaterialDestroy(ctx, out);
            return r;
        }
    }
    return (VulkanResult){.status = VULKAN_SUCCESS, .vk_result = VK_SUCCESS};
}

void vkMaterialDestroy(VkContext* ctx, VkMaterial* mat) {
    for (uint32_t i = 0; i < mat->uboCount; i++) {
        MaterialUBOBuffer* ubo = &mat->uboBuffers[i];
        for (uint32_t f = 0; f < MAX_FRAMES_IN_FLIGHT; f++)
            vkBufferDestroy(ctx, &ubo->gpuBuffers[f]);
        free(ubo->data);
    }
    if (mat->pool      != VK_NULL_HANDLE) vkDestroyDescriptorPool(ctx->logicalDevice, mat->pool, NULL);
    if (mat->setLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(ctx->logicalDevice, mat->setLayout, NULL);

    for (uint32_t i = 0; i < PASS_TYPE_COUNT; i++) {
        if (mat->pipelines[i]      != VK_NULL_HANDLE) vkDestroyPipeline(ctx->logicalDevice, mat->pipelines[i], NULL);
        if (mat->pipelineLayouts[i]!= VK_NULL_HANDLE) vkDestroyPipelineLayout(ctx->logicalDevice, mat->pipelineLayouts[i], NULL);
    }
    memset(mat, 0, sizeof(*mat));
}

VkPipelineBindPoint vkMaterialGetBindPoint(VkMaterial* mat) {
    return mat->isCompute ? VK_PIPELINE_BIND_POINT_COMPUTE : VK_PIPELINE_BIND_POINT_GRAPHICS;
}

void vkMaterialBindPipeline(VkCommandBuffer cmd, VkMaterial* mat, VkPassType passType) {
    VkPipeline pipeline = mat->pipelines[passType];
    if (pipeline == VK_NULL_HANDLE) {
        LOG_WARN("vkMaterialBindPipeline: no pipeline for passType %d", passType);
        return;
    }
    vkCmdBindPipeline(cmd, vkMaterialGetBindPoint(mat), pipeline);
}



static MaterialUBOBuffer* find_ubo(VkMaterial* mat, uint32_t binding) {
    for (uint32_t i = 0; i < mat->uboCount; i++)
        if (mat->uboBuffers[i].binding == binding) return &mat->uboBuffers[i];
    return NULL;
}

bool vkMaterialSetParam(VkMaterial* mat, const char* name, MaterialParamValue value) {
    for (uint32_t i = 0; i < mat->paramCount; i++) {
        MaterialParamEntry* e = &mat->params[i];
        if (strncmp(e->name, name, MATERIAL_NAME_MAX) != 0) continue;

        MaterialUBOBuffer* ubo = find_ubo(mat, e->binding);
        if (!ubo) return false;
        void* dst = ubo->data + e->offset;
        switch (value.type) {
            case MATERIAL_PARAM_FLOAT:  memcpy(dst, &value.f,   4);  break;
            case MATERIAL_PARAM_FLOAT2: memcpy(dst, value.f2,   8);  break;
            case MATERIAL_PARAM_FLOAT3: memcpy(dst, value.f3,   12); break;
            case MATERIAL_PARAM_FLOAT4: memcpy(dst, value.f4,   16); break;
            case MATERIAL_PARAM_INT:    memcpy(dst, &value.i,   4);  break;
            case MATERIAL_PARAM_UINT:   memcpy(dst, &value.u,   4);  break;
            case MATERIAL_PARAM_MAT4:   memcpy(dst, value.mat4, 64); break;
            default: return false;
        }
        for (uint32_t f = 0; f < MAX_FRAMES_IN_FLIGHT; f++) mat->needsFlush[f] = true;
        return true;
    }
    LOG_WARN("vkMaterialSetParam: no param named '%s'", name);
    return false;
}

void vkMaterialSetParams(VkMaterial* mat, const void* srcStruct,
                          const MaterialFieldDesc* fields, uint32_t fieldCount)
{
    for (uint32_t i = 0; i < fieldCount; i++) {
        const MaterialFieldDesc* f = &fields[i];
        const void* src = (const uint8_t*)srcStruct + f->offset;
        MaterialParamValue v = { .type = f->type };
        switch (f->type) {
            case MATERIAL_PARAM_FLOAT:  memcpy(&v.f,   src, 4);  break;
            case MATERIAL_PARAM_FLOAT2: memcpy(v.f2,   src, 8);  break;
            case MATERIAL_PARAM_FLOAT3: memcpy(v.f3,   src, 12); break;
            case MATERIAL_PARAM_FLOAT4: memcpy(v.f4,   src, 16); break;
            case MATERIAL_PARAM_INT:    memcpy(&v.i,   src, 4);  break;
            case MATERIAL_PARAM_UINT:   memcpy(&v.u,   src, 4);  break;
            case MATERIAL_PARAM_MAT4:   memcpy(v.mat4, src, 64); break;
            default: break;
        }
        vkMaterialSetParam(mat, f->name, v);
    }
}

void vkMaterialFlush(VkMaterial* mat, VkDevice device, uint32_t frameIndex) {
    if (!mat->hasSet || !mat->needsFlush[frameIndex]) return;

    VkWriteDescriptorSet  writes[MATERIAL_MAX_BINDINGS];
    VkDescriptorBufferInfo bufInfos[MATERIAL_MAX_BINDINGS];
    uint32_t writeCount = 0;

    for (uint32_t i = 0; i < mat->uboCount; i++) {
        MaterialUBOBuffer* ubo = &mat->uboBuffers[i];
        memcpy(ubo->gpuBuffers[frameIndex].mappedData, ubo->data, ubo->totalSize);

        bufInfos[i] = (VkDescriptorBufferInfo){
            .buffer = ubo->gpuBuffers[frameIndex].buffer,
            .offset = 0,
            .range  = ubo->totalSize,
        };
        writes[writeCount++] = (VkWriteDescriptorSet){
            .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet          = mat->sets[frameIndex],
            .dstBinding      = ubo->binding,
            .descriptorCount = 1,
            .descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .pBufferInfo     = &bufInfos[i],
        };
    }

    if (writeCount > 0)
        vkUpdateDescriptorSets(device, writeCount, writes, 0, NULL);
    mat->needsFlush[frameIndex] = false;
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