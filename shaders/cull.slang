struct DrawCommand {
    uint indexCount;
    uint instanceCount;
    uint firstIndex;
    int  vertexOffset;
    uint firstInstance;
};
struct ObjectData {
    float4x4 transform;
    float3 localMin;
    float pad0;
    float3 localMax;
    uint indexCount;
    uint firstIndex;
    int vertexOffset;
    uint pad1;
};
struct CullUBO {
    float4   frustumPlanes[6];
    uint     objectCount;
};
[[vk::binding(0)]] ConstantBuffer<CullUBO>           ubo;
[[vk::binding(1)]] StructuredBuffer<ObjectData>      objects;
[[vk::binding(2)]] RWStructuredBuffer<DrawCommand>   drawCommands;
[[vk::binding(3)]] RWStructuredBuffer<uint>          drawCount;

bool sphereInFrustum(float3 center, float radius, float4 planes[6]) {
    for (int i = 0; i < 6; i++)
        if (dot(planes[i].xyz, center) + planes[i].w + radius < 0.0)
            return false;
    return true;
}
float3 transformPoint(float4x4 m, float3 p) {
    float4 r = mul(m, float4(p, 1.0));
    return r.xyz / r.w;
}

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID) {
    uint idx = tid.x;
    if (idx >= ubo.objectCount) return;

    ObjectData obj = objects[idx];

    float3 extents = obj.localMax - obj.localMin;
    bool zeroAABB = dot(extents, extents) < 1e-10;

    if (!zeroAABB) {
        float3 corners[8] = {
            float3(obj.localMin.x, obj.localMin.y, obj.localMin.z),
            float3(obj.localMax.x, obj.localMin.y, obj.localMin.z),
            float3(obj.localMin.x, obj.localMax.y, obj.localMin.z),
            float3(obj.localMax.x, obj.localMax.y, obj.localMin.z),
            float3(obj.localMin.x, obj.localMin.y, obj.localMax.z),
            float3(obj.localMax.x, obj.localMin.y, obj.localMax.z),
            float3(obj.localMin.x, obj.localMax.y, obj.localMax.z),
            float3(obj.localMax.x, obj.localMax.y, obj.localMax.z),
        };
        float3 worldMin = float3( 1e30,  1e30,  1e30);
        float3 worldMax = float3(-1e30, -1e30, -1e30);
        for (int i = 0; i < 8; i++) {
            float3 w = transformPoint(obj.transform, corners[i]);
            worldMin = min(worldMin, w);
            worldMax = max(worldMax, w);
        }
        float3 center = (worldMin + worldMax) * 0.5;
        float  radius = length(worldMax - worldMin) * 0.5;
        if (!sphereInFrustum(center, radius, ubo.frustumPlanes)) return;
    }

    uint slot;
    InterlockedAdd(drawCount[0], 1, slot);
    DrawCommand cmd;
    cmd.indexCount = obj.indexCount; // Use object metadata
    cmd.instanceCount = 1;
    cmd.firstIndex = obj.firstIndex;     // Use object metadata
    cmd.vertexOffset = obj.vertexOffset; // Use object metadata
    cmd.firstInstance = idx;
    drawCommands[slot] = cmd;
}