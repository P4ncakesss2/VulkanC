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
#include "depth_prepass.h"

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

#define MESH_AMOUNT 8
#define SPHERE_STACKS  32
#define SPHERE_SLICES  32

static Vertex   s_sphereVertices[(SPHERE_STACKS + 1) * (SPHERE_SLICES + 1)];
static uint32_t s_sphereIndices[SPHERE_STACKS * SPHERE_SLICES * 6];

static void build_sphere(float radius) {
    uint32_t vi = 0;
    for (int stack = 0; stack <= SPHERE_STACKS; stack++) {
        float phi    = GLM_PI * ((float)stack / (float)SPHERE_STACKS);
        float y      = cosf(phi);
        float r      = sinf(phi);

        for (int slice = 0; slice <= SPHERE_SLICES; slice++) {
            float theta = 2.0f * GLM_PI * ((float)slice / (float)SPHERE_SLICES);
            float x     = r * cosf(theta);
            float z     = r * sinf(theta);

            float nx = x, ny = y, nz = z;
            float tx = -sinf(theta), ty = 0.0f, tz = cosf(theta);
            float u  = (float)slice / (float)SPHERE_SLICES;
            float v  = (float)stack / (float)SPHERE_STACKS;

            s_sphereVertices[vi++] = (Vertex){
                .position = { x * radius, y * radius, z * radius },
                .color    = { 1, 1, 1 },
                .normal   = { nx, ny, nz },
                .uv       = { u, v },
                .tangent  = { tx, ty, tz, 1.0f },
            };
        }
    }

    uint32_t ii = 0;
    for (int stack = 0; stack < SPHERE_STACKS; stack++) {
        for (int slice = 0; slice < SPHERE_SLICES; slice++) {
            uint32_t a = stack       * (SPHERE_SLICES + 1) + slice;
            uint32_t b = (stack + 1) * (SPHERE_SLICES + 1) + slice;
            s_sphereIndices[ii++] = a;
            s_sphereIndices[ii++] = a + 1;
            s_sphereIndices[ii++] = b;
            s_sphereIndices[ii++] = b;
            s_sphereIndices[ii++] = a + 1;
            s_sphereIndices[ii++] = b + 1;
        }
    }
}

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
        .validationLayers   = false,
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

    VkTexture boxTex, boxNormalTex, boxRoughnessTex, boxMetallicTex, skyboxTex, irradianceTex, radianceTex, brdfLutTex;
    uint32_t  boxTexId = 0, boxNormalTexId = 0, boxRoughnessTexId = 0, boxMetallicTexId = 0, skyboxTexId = 0, irradianceTexId = 0, radianceTexId = 0, brdfLutTexId = 0;
    vkTextureLoad(&ctx, "assets/scratchedMetal/Metal055A_2K-PNG_Color.png", &boxTex, &boxTexId, false);
    vkTextureLoad(&ctx, "assets/scratchedMetal/Metal055A_2K-PNG_NormalGL.png", &boxNormalTex, &boxNormalTexId, true);
    vkTextureLoad(&ctx, "assets/scratchedMetal/Metal055A_2K-PNG_Roughness.png", &boxRoughnessTex, &boxRoughnessTexId, true);
    vkTextureLoad(&ctx, "assets/scratchedMetal/Metal055A_2K-PNG_Metalness.png", &boxMetallicTex, &boxMetallicTexId, true);

    const char* skyboxFaces[6] = {
        "assets/skybox/right.jpg", "assets/skybox/left.jpg",
        "assets/skybox/top.jpg",   "assets/skybox/bottom.jpg",
        "assets/skybox/front.jpg", "assets/skybox/back.jpg",
    };
    vkTextureLoadCubemap(&ctx, skyboxFaces, &skyboxTex, &skyboxTexId);

    const char* irradianceFaces[6] = {
        "assets/ibl/irradiance/output_pmrem_negx_1_128x128_posx.tga", // right
        "assets/ibl/irradiance/output_pmrem_negx_1_128x128_negx.tga", // left
        "assets/ibl/irradiance/output_pmrem_negx_1_128x128_posy.tga", // top
        "assets/ibl/irradiance/output_pmrem_negx_1_128x128_negy.tga", // bottom
        "assets/ibl/irradiance/output_pmrem_negx_1_128x128_posz.tga", // front
        "assets/ibl/irradiance/output_pmrem_negx_1_128x128_negz.tga", // back
    };
    vkTextureLoadCubemap(&ctx, irradianceFaces, &irradianceTex, &irradianceTexId);

    const char* radianceFaces[7][6] = {
        {
        "assets/ibl/radiance/output_pmrem_posx_0_256x256.tga", "assets/ibl/radiance/output_pmrem_negx_0_256x256.tga",
        "assets/ibl/radiance/output_pmrem_posy_0_256x256.tga", "assets/ibl/radiance/output_pmrem_negy_0_256x256.tga",
        "assets/ibl/radiance/output_pmrem_posz_0_256x256.tga", "assets/ibl/radiance/output_pmrem_negz_0_256x256.tga",
        },
        {
        "assets/ibl/radiance/output_pmrem_posx_1_128x128.tga", "assets/ibl/radiance/output_pmrem_negx_1_128x128.tga",
        "assets/ibl/radiance/output_pmrem_posy_1_128x128.tga", "assets/ibl/radiance/output_pmrem_negy_1_128x128.tga",
        "assets/ibl/radiance/output_pmrem_posz_1_128x128.tga", "assets/ibl/radiance/output_pmrem_negz_1_128x128.tga",
        },
        {
        "assets/ibl/radiance/output_pmrem_posx_2_64x64.tga",   "assets/ibl/radiance/output_pmrem_negx_2_64x64.tga",
        "assets/ibl/radiance/output_pmrem_posy_2_64x64.tga",   "assets/ibl/radiance/output_pmrem_negy_2_64x64.tga",
        "assets/ibl/radiance/output_pmrem_posz_2_64x64.tga",   "assets/ibl/radiance/output_pmrem_negz_2_64x64.tga",
        },
        {
        "assets/ibl/radiance/output_pmrem_posx_3_32x32.tga",   "assets/ibl/radiance/output_pmrem_negx_3_32x32.tga",
        "assets/ibl/radiance/output_pmrem_posy_3_32x32.tga",   "assets/ibl/radiance/output_pmrem_negy_3_32x32.tga",
        "assets/ibl/radiance/output_pmrem_posz_3_32x32.tga",   "assets/ibl/radiance/output_pmrem_negz_3_32x32.tga",
        },
        {
        "assets/ibl/radiance/output_pmrem_posx_4_16x16.tga",   "assets/ibl/radiance/output_pmrem_negx_4_16x16.tga",
        "assets/ibl/radiance/output_pmrem_posy_4_16x16.tga",   "assets/ibl/radiance/output_pmrem_negy_4_16x16.tga",
        "assets/ibl/radiance/output_pmrem_posz_4_16x16.tga",   "assets/ibl/radiance/output_pmrem_negz_4_16x16.tga",
        },
        {
        "assets/ibl/radiance/output_pmrem_posx_5_8x8.tga",     "assets/ibl/radiance/output_pmrem_negx_5_8x8.tga",
        "assets/ibl/radiance/output_pmrem_posy_5_8x8.tga",     "assets/ibl/radiance/output_pmrem_negy_5_8x8.tga",
        "assets/ibl/radiance/output_pmrem_posz_5_8x8.tga",     "assets/ibl/radiance/output_pmrem_negz_5_8x8.tga",
        },
        {
        "assets/ibl/radiance/output_pmrem_posx_6_4x4.tga",     "assets/ibl/radiance/output_pmrem_negx_6_4x4.tga",
        "assets/ibl/radiance/output_pmrem_posy_6_4x4.tga",     "assets/ibl/radiance/output_pmrem_negy_6_4x4.tga",
        "assets/ibl/radiance/output_pmrem_posz_6_4x4.tga",     "assets/ibl/radiance/output_pmrem_negz_6_4x4.tga",
        }
    };
    
    vkTextureLoadCubemapPrecompiled(&ctx, radianceFaces, 7, &radianceTex, &radianceTexId);

    vkTextureLoad(&ctx, "assets/ibl/brdf_lut.png", &brdfLutTex, &brdfLutTexId, true);

    build_sphere(0.5f);
    VkMesh boxMesh;
    VkMeshCreateInfo boxMeshInfo = {
        .vertexArray = s_sphereVertices, .indexArray = s_sphereIndices,
        .vertexCount = sizeof(s_sphereVertices) / sizeof(s_sphereVertices[0]),
        .indexCount  = sizeof(s_sphereIndices)  / sizeof(s_sphereIndices[0]),
    };
    vkMeshCreate(&ctx, &boxMeshInfo, &boxMesh);

    VkShaderStageCreateInfo meshStages[] = {
        { VK_SHADER_STAGE_VERTEX_BIT,   PASS_TYPE_GEOMETRY, "shaders/shader.spv",  "vertMain" },
        { VK_SHADER_STAGE_FRAGMENT_BIT, PASS_TYPE_GEOMETRY, "shaders/shader.spv",  "fragMain" },
        { VK_SHADER_STAGE_VERTEX_BIT,   PASS_TYPE_DEPTH,    "shaders/shader.spv",  "vertMain" },
    };
    VkShaderStageCreateInfo skyboxStages[] = {
        { VK_SHADER_STAGE_VERTEX_BIT,   PASS_TYPE_GEOMETRY, "shaders/skybox.spv", "vertMain" },
        { VK_SHADER_STAGE_FRAGMENT_BIT, PASS_TYPE_GEOMETRY, "shaders/skybox.spv", "fragMain" },
    };

    VkMaterial boxMaterial, skyboxMaterial;

    VkPipelineBuilder boxBuilder = vkPipelineBuilderCreateDefault();
    boxBuilder.stages      = meshStages;
    boxBuilder.stageCount  = 3;
    boxBuilder.colorFormat = window.swapChainSurfaceFormat.format;
    boxBuilder.depthFormat = VK_FORMAT_D32_SFLOAT;
    boxBuilder.msaaSamples = ctx.msaaSamples;
    vkMaterialInit(&boxMaterial, &boxBuilder);
    
    vkMaterialSetParam(&boxMaterial, "colorMapIndex", mpUint(boxTexId));
    vkMaterialSetParam(&boxMaterial, "normalMapIndex", mpUint(boxNormalTexId));
    vkMaterialSetParam(&boxMaterial, "roughnessMapIndex", mpUint(boxRoughnessTexId));
    vkMaterialSetParam(&boxMaterial, "metallicMapIndex",  mpUint(boxMetallicTexId));

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

    objects[objectCount] = (RenderObject){
        .mesh     = NULL,             
        .material = &skyboxMaterial,
    };
    glm_mat4_identity(objects[objectCount].transform);
    objectCount++;

    GeometryPassData geometryData = {0};
    CullPassData     cullData     = {0};

    geometryData.objects     = objects;
    geometryData.objectCount = objectCount;

    build_batches(&geometryData);

    cullData.objects      = objects;
    cullData.objectCount  = &geometryData.objectCount;
    cullData.geometryData = &geometryData;
    geometryData.cullData = &cullData;

    geometryData.ubo.irradianceMapIndex = irradianceTexId;
    geometryData.ubo.radianceMapIndex = radianceTexId;
    geometryData.ubo.brdfLutIndex = brdfLutTexId;
    geometryData.ubo.radianceMipCount = radianceTex.mipLevels;

    DepthPrepassData depthPrepassData = {0};
    depthPrepassData.geometryData = &geometryData;
    depthPrepassData.cullData     = &cullData;

    RenderPass cullPass     = cullPassCreate(&cullData);
    RenderPass geometryPass = geometryPassCreate(&geometryData);
    RenderPass depthPrepass = depthPrepassCreate(&depthPrepassData);
    vkRendererAddPass(&renderer, &depthPrepass);
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

        int cubeIndex = 0;
        for (uint32_t i = 0; i < objectCount; i++) {
            if (objects[i].mesh != NULL) {
                float totalWidth = (MESH_AMOUNT - 1) * 1.5f;
                float startX = -totalWidth / 2.0f;
                vec3 pos = { startX + cubeIndex * 1.5f, 0.0f, 3.0f };
                vec3 axis = {0, 1, 0};
                glm_mat4_identity(objects[i].transform);
                glm_translate(objects[i].transform, pos);
                glm_rotate(objects[i].transform, angle + cubeIndex, axis);
                cubeIndex++;
            } else {
                glm_mat4_identity(objects[i].transform);
            }
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

        geometryData.ubo.cameraPos[0] = s_cameraPos[0];
        geometryData.ubo.cameraPos[1] = s_cameraPos[1];
        geometryData.ubo.cameraPos[2] = s_cameraPos[2];
        geometryData.ubo.cameraPos[3] = 1.0f;

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
    vkTextureDestroy(&ctx, &brdfLutTex);
    vkTextureDestroy(&ctx, &irradianceTex);
    vkTextureDestroy(&ctx, &radianceTex);
    vkTextureDestroy(&ctx, &boxTex);
    vkTextureDestroy(&ctx, &boxNormalTex);
    vkTextureDestroy(&ctx, &boxRoughnessTex);
    vkTextureDestroy(&ctx, &boxMetallicTex);
    vkTextureDestroy(&ctx, &skyboxTex);
    vkMeshDestroy(&ctx, &boxMesh);
    vkWindowDestroy(&ctx, &window);
    vkContextDestroy(&ctx);
    return 0;
}