#include "mesh.h"
#include "vulkan_ctx.h"
#include "window.h"
#include "render_types.h"
#include "logger.h"
#include "texture.h"
#include "material.h"
#include "renderer.h"
#include "geometry_pass.h"
#include "cull_pass.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static vec3  s_cameraPos   = {0.0f, 0.0f, -2.0f};
static vec3  s_cameraFront = {0.0f, 0.0f,  1.0f};
static vec3  s_cameraUp    = {0.0f, 1.0f,  0.0f};
static bool  s_firstMouse  = true;
static float s_yaw         = 90.0f, s_pitch = 0.0f;
static float s_lastX       = 400.0f, s_lastY = 300.0f;

static void mouse_callback(GLFWwindow* win, double xposIn, double yposIn) {
    (void)win;
    float xpos = (float)xposIn, ypos = (float)yposIn;
    if (s_firstMouse) { s_lastX = xpos; s_lastY = ypos; s_firstMouse = false; }
    float xoff = (xpos - s_lastX) * 0.1f;
    float yoff = (s_lastY - ypos) * 0.1f;
    s_lastX = xpos; s_lastY = ypos;
    s_yaw   += xoff;
    s_pitch  = glm_clamp(s_pitch + yoff, -89.0f, 89.0f);
    vec3 front = {
        cosf(glm_rad(s_yaw)) * cosf(glm_rad(s_pitch)),
        sinf(glm_rad(s_pitch)),
        sinf(glm_rad(s_yaw)) * cosf(glm_rad(s_pitch)),
    };
    glm_vec3_normalize_to(front, s_cameraFront);
}

static void process_input(GLFWwindow* win, float dt) {
    if (glfwGetKey(win, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(win, true);
    float speed = 5.0f * dt;
    vec3 tmp;
    if (glfwGetKey(win, GLFW_KEY_W)          == GLFW_PRESS) { glm_vec3_scale(s_cameraFront, speed, tmp); glm_vec3_add(s_cameraPos, tmp, s_cameraPos); }
    if (glfwGetKey(win, GLFW_KEY_S)          == GLFW_PRESS) { glm_vec3_scale(s_cameraFront, speed, tmp); glm_vec3_sub(s_cameraPos, tmp, s_cameraPos); }
    if (glfwGetKey(win, GLFW_KEY_A)          == GLFW_PRESS) { glm_vec3_cross(s_cameraFront, s_cameraUp, tmp); glm_vec3_normalize(tmp); glm_vec3_scale(tmp, speed, tmp); glm_vec3_sub(s_cameraPos, tmp, s_cameraPos); }
    if (glfwGetKey(win, GLFW_KEY_D)          == GLFW_PRESS) { glm_vec3_cross(s_cameraFront, s_cameraUp, tmp); glm_vec3_normalize(tmp); glm_vec3_scale(tmp, speed, tmp); glm_vec3_add(s_cameraPos, tmp, s_cameraPos); }
    if (glfwGetKey(win, GLFW_KEY_SPACE)      == GLFW_PRESS) { glm_vec3_scale(s_cameraUp, speed, tmp); glm_vec3_add(s_cameraPos, tmp, s_cameraPos); }
    if (glfwGetKey(win, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) { glm_vec3_scale(s_cameraUp, speed, tmp); glm_vec3_sub(s_cameraPos, tmp, s_cameraPos); }
}

#define MESH_AMOUNT 5

static Vertex s_boxVertices[] = {
    {{-0.5f,-0.5f, 0.5f},{1,1,1},{0,0, 1},{0,0}}, {{ 0.5f,-0.5f, 0.5f},{1,1,1},{0,0, 1},{1,0}},
    {{ 0.5f, 0.5f, 0.5f},{1,1,1},{0,0, 1},{1,1}}, {{-0.5f, 0.5f, 0.5f},{1,1,1},{0,0, 1},{0,1}},
    {{ 0.5f,-0.5f,-0.5f},{1,1,1},{0,0,-1},{0,0}}, {{-0.5f,-0.5f,-0.5f},{1,1,1},{0,0,-1},{1,0}},
    {{-0.5f, 0.5f,-0.5f},{1,1,1},{0,0,-1},{1,1}}, {{ 0.5f, 0.5f,-0.5f},{1,1,1},{0,0,-1},{0,1}},
    {{-0.5f,-0.5f,-0.5f},{1,1,1},{-1,0,0},{0,0}}, {{-0.5f,-0.5f, 0.5f},{1,1,1},{-1,0,0},{1,0}},
    {{-0.5f, 0.5f, 0.5f},{1,1,1},{-1,0,0},{1,1}}, {{-0.5f, 0.5f,-0.5f},{1,1,1},{-1,0,0},{0,1}},
    {{ 0.5f,-0.5f, 0.5f},{1,1,1},{ 1,0,0},{0,0}}, {{ 0.5f,-0.5f,-0.5f},{1,1,1},{ 1,0,0},{1,0}},
    {{ 0.5f, 0.5f,-0.5f},{1,1,1},{ 1,0,0},{1,1}}, {{ 0.5f, 0.5f, 0.5f},{1,1,1},{ 1,0,0},{0,1}},
    {{-0.5f,-0.5f,-0.5f},{1,1,1},{0,-1,0},{0,0}}, {{ 0.5f,-0.5f,-0.5f},{1,1,1},{0,-1,0},{1,0}},
    {{ 0.5f,-0.5f, 0.5f},{1,1,1},{0,-1,0},{1,1}}, {{-0.5f,-0.5f, 0.5f},{1,1,1},{0,-1,0},{0,1}},
    {{-0.5f, 0.5f, 0.5f},{1,1,1},{0, 1,0},{0,0}}, {{ 0.5f, 0.5f, 0.5f},{1,1,1},{0, 1,0},{1,0}},
    {{ 0.5f, 0.5f,-0.5f},{1,1,1},{0, 1,0},{1,1}}, {{-0.5f, 0.5f,-0.5f},{1,1,1},{0, 1,0},{0,1}},
};
static uint32_t s_boxIndices[] = {
     0, 1, 2,  2, 3, 0,   4, 5, 6,  6, 7, 4,
     8, 9,10, 10,11, 8,  12,13,14, 14,15,12,
    16,17,18, 18,19,16,  20,21,22, 22,23,20,
};

static void build_batches(GeometryPassData* geo) {
    RenderObject* objects = geo->objects;
    uint32_t      count   = geo->objectCount;

    for (uint32_t i = 1; i < count; i++) {
        RenderObject tmp = objects[i];
        uint32_t j = i;
        while (j > 0 && objects[j-1].material > tmp.material) {
            objects[j] = objects[j-1];
            j--;
        }
        objects[j] = tmp;
    }

    geo->batchCount = 0;
    uint32_t drawCmdOffset = 0;
    for (uint32_t i = 0; i < count; ) {
        uint32_t j = i + 1;
        while (j < count && objects[j].material == objects[i].material)
            j++;

        GeometryBatch* b        = &geo->batches[geo->batchCount++];
        b->representativeObject = &objects[i];
        b->drawCommandOffset    = drawCmdOffset;
        b->maxDrawCount         = j - i;
        drawCmdOffset          += j - i;
        i = j;
    }
}

int main(void) {
    VkContext ctx;
    VkContextCreateInfo ctxInfo = {
        .appName            = "Renderer Demo",
        .enablePresentation = true,
        .validationLayers   = true,
        .msaaSamples        = VK_SAMPLE_COUNT_8_BIT,
    };
    VulkanResult result = vkContextCreate(&ctxInfo, &ctx);
    if (result.status != VULKAN_SUCCESS) { LOG_ERROR("Context creation failed"); return 1; }

    VkWindow window;
    VkWindowCreateInfo winInfo = { .title = "Renderer Demo", .width = 800, .height = 600 };
    result = vkWindowCreate(&ctx, &winInfo, &window);
    if (result.status != VULKAN_SUCCESS) { LOG_ERROR("Window creation failed"); return 1; }

    glfwSetCursorPosCallback(window.handle, mouse_callback);
    glfwSetInputMode(window.handle, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    VkRenderer renderer;
    result = vkRendererCreate(&ctx, &window, &renderer);
    if (result.status != VULKAN_SUCCESS) { LOG_ERROR("Renderer creation failed"); return 1; }

    VkTexture boxTex, skyboxTex;
    uint32_t  boxTexId = 0, skyboxTexId = 0;
    vkTextureLoad(&ctx, "assets/box.jpg", &boxTex, &boxTexId);
    const char* skyboxFaces[6] = {
        "assets/skybox/right.jpg", "assets/skybox/left.jpg",
        "assets/skybox/top.jpg",   "assets/skybox/bottom.jpg",
        "assets/skybox/front.jpg", "assets/skybox/back.jpg",
    };
    vkTextureLoadCubemap(&ctx, skyboxFaces, &skyboxTex, &skyboxTexId);

    VkMesh boxMesh;
    VkMeshCreateInfo boxMeshInfo = {
        .vertexArray = s_boxVertices, .indexArray = s_boxIndices,
        .vertexCount = sizeof(s_boxVertices) / sizeof(s_boxVertices[0]),
        .indexCount  = sizeof(s_boxIndices)  / sizeof(s_boxIndices[0]),
    };
    vkMeshCreate(&ctx, &boxMeshInfo, &boxMesh);

    VkShaderStageCreateInfo meshStages[] = {
        { VK_SHADER_STAGE_VERTEX_BIT,   PASS_TYPE_GEOMETRY, "shaders/slang.spv",  "vertMain" },
        { VK_SHADER_STAGE_FRAGMENT_BIT, PASS_TYPE_GEOMETRY, "shaders/slang.spv",  "fragMain" },
    };
    VkShaderStageCreateInfo skyboxStages[] = {
        { VK_SHADER_STAGE_VERTEX_BIT,   PASS_TYPE_GEOMETRY, "shaders/skybox.spv", "vertMain" },
        { VK_SHADER_STAGE_FRAGMENT_BIT, PASS_TYPE_GEOMETRY, "shaders/skybox.spv", "fragMain" },
    };

    VkMaterial boxMaterial, skyboxMaterial;

    VkPipelineBuilder boxBuilder = vkPipelineBuilderCreateDefault();
    boxBuilder.stages      = meshStages;
    boxBuilder.stageCount  = 2;
    boxBuilder.colorFormat = window.swapChainSurfaceFormat.format;
    boxBuilder.depthFormat = VK_FORMAT_D32_SFLOAT;
    boxBuilder.msaaSamples = ctx.msaaSamples;
    vkMaterialInit(&boxMaterial, &boxBuilder);
    vkMaterialSetParam(&boxMaterial, "textureIndex", mpUint(boxTexId));

    VkPipelineBuilder skyboxBuilder    = vkPipelineBuilderCreateDefault();
    skyboxBuilder.stages               = skyboxStages;
    skyboxBuilder.stageCount           = 2;
    skyboxBuilder.colorFormat          = window.swapChainSurfaceFormat.format;
    skyboxBuilder.depthFormat          = VK_FORMAT_D32_SFLOAT;
    skyboxBuilder.msaaSamples          = ctx.msaaSamples;
    skyboxBuilder.cullMode             = VK_CULL_MODE_NONE;
    skyboxBuilder.depthWriteEnable     = VK_FALSE;
    skyboxBuilder.depthCompareOp       = VK_COMPARE_OP_LESS_OR_EQUAL;
    skyboxBuilder.vertexBindingCount   = 0;
    skyboxBuilder.vertexAttributeCount = 0;
    vkMaterialInit(&skyboxMaterial, &skyboxBuilder);
    vkMaterialSetParam(&skyboxMaterial, "textureIndex", mpUint(skyboxTexId));

    RenderObject objects[MAX_OBJECTS];
    uint32_t     objectCount = 0;
    for (int i = 0; i < MESH_AMOUNT; i++) {
        objects[objectCount] = (RenderObject){
            .mesh     = &boxMesh,
            .material = &boxMaterial,
        };
        glm_mat4_identity(objects[objectCount].transform);
        objectCount++;
    }

    GeometryPassData geometryData = {0};
    CullPassData     cullData     = {0};

    geometryData.objects     = objects;
    geometryData.objectCount = objectCount;

    build_batches(&geometryData);

    cullData.objects      = objects;
    cullData.objectCount  = &geometryData.objectCount;
    cullData.geometryData = &geometryData;
    geometryData.cullData = &cullData;

    RenderPass cullPass     = cullPassCreate(&cullData);
    RenderPass geometryPass = geometryPassCreate(&geometryData);
    vkRendererAddPass(&renderer, &cullPass);
    vkRendererAddPass(&renderer, &geometryPass);

    double lastTime   = glfwGetTime();
    double fpsTimer   = lastTime;
    int    frameCount = 0;
    float  angle      = 0.0f;

    while (!vkWindowShouldClose(&window)) {
        glfwPollEvents();

        double now = glfwGetTime();
        float  dt  = (float)(now - lastTime);
        lastTime   = now;
        angle      = fmodf(angle + dt, 2.0f * GLM_PI);

        frameCount++;
        if (now - fpsTimer >= 1.0) {
            char title[128];
            snprintf(title, sizeof(title), "Renderer Demo — %.1f FPS",
                     (double)frameCount / (now - fpsTimer));
            glfwSetWindowTitle(window.handle, title);
            frameCount = 0;
            fpsTimer   = now;
        }

        process_input(window.handle, dt);

        float totalWidth = (MESH_AMOUNT - 1) * 1.5f, startX = -totalWidth / 2.0f;
        for (int i = 0; i < MESH_AMOUNT; i++) {
            vec3 pos = { startX + i * 1.5f, 0.0f, 3.0f }, axis = {0, 1, 0};
            glm_mat4_identity(objects[i].transform);
            glm_translate(objects[i].transform, pos);
            glm_rotate(objects[i].transform, angle + i, axis);
        }

        GlobalUBO ubo = { .time = (float)now };
        memcpy((char*)ctx.uniformBuffer.mappedData +
               renderer.frameIndex * ctx.uniformFrameStride,
               &ubo, sizeof(GlobalUBO));

        ObjectSSBO* ssbo = (ObjectSSBO*)(
            (char*)ctx.objectStorageBuffer.mappedData +
            renderer.frameIndex * ctx.objectFrameStride);
        for (uint32_t i = 0; i < objectCount; i++) {
            glm_mat4_copy(objects[i].transform, ssbo[i].transform);
            if (objects[i].mesh) {
                glm_vec3_copy(objects[i].mesh->local_min, ssbo[i].local_min);
                glm_vec3_copy(objects[i].mesh->local_max, ssbo[i].local_max);

                ssbo[i].indexCount   = objects[i].mesh->indexCount;
                ssbo[i].firstIndex   = 0;
                ssbo[i].vertexOffset = 0;
            } else {
                glm_vec3_zero(ssbo[i].local_min);
                glm_vec3_zero(ssbo[i].local_max);
                ssbo[i].indexCount   = 0;
                ssbo[i].firstIndex   = 0;
                ssbo[i].vertexOffset = 0;
            }
        }

        float aspect = (float)window.swapChainExtent.width /
                       (float)window.swapChainExtent.height;
        vec3 center;
        glm_vec3_add(s_cameraPos, s_cameraFront, center);
        glm_lookat(s_cameraPos, center, s_cameraUp, geometryData.ubo.view);
        glm_perspective(glm_rad(60.0f), aspect, 0.1f, 100.0f, geometryData.ubo.proj);
        geometryData.ubo.proj[1][1] *= -1.0f;
        glm_mat4_inv(geometryData.ubo.proj, geometryData.ubo.invProj);

        result = vkRendererBeginFrame(&renderer);
        if (result.status == VULKAN_STATUS_SWAPCHAIN_OUTDATED) {
            vkRendererResize(&renderer);
            continue;
        }
        if (result.status != VULKAN_SUCCESS) { LOG_ERROR("BeginFrame failed"); break; }

        vkRendererExecutePasses(&renderer, dt, (float)now);

        result = vkRendererEndFrame(&renderer);
        if (result.status != VULKAN_SUCCESS) { LOG_ERROR("EndFrame failed"); break; }

        if (vkRendererNeedsResize(&renderer))
            vkRendererResize(&renderer);
    }

    vkDeviceWaitIdle(ctx.logicalDevice);

    vkRendererDestroy(&renderer);

    vkTextureDestroy(&ctx, &boxTex);
    vkTextureDestroy(&ctx, &skyboxTex);
    vkMeshDestroy(&ctx, &boxMesh);
    vkWindowDestroy(&ctx, &window);
    vkContextDestroy(&ctx);
    return 0;
}