struct GlobalUBO {
    float time;
};
[[vk::binding(0, 0)]] ConstantBuffer<GlobalUBO> globalUBO;

struct GeometryPassData {
    float4x4 proj;
    float4x4 view;
    float4x4 invProj;
};
[[vk::binding(0, 1)]] ConstantBuffer<GeometryPassData> passData;

struct ObjectData {
    float4x4 transform;
    float3 localMin;
    float pad0;
    float3 localMax;
    float pad1;
};
[[vk::binding(1, 0)]] StructuredBuffer<ObjectData> objectData;
[[vk::binding(2, 0)]] TextureCube                  cubemapArray[];
[[vk::binding(3, 0)]] SamplerState                 globalSampler;

struct MaterialParams {
    uint textureIndex;
};
[[vk::binding(0, 2)]] ConstantBuffer<MaterialParams> matParams;

struct VertexOutput {
    float3 viewRay : TEXCOORD0; 
    float4 sv_position : SV_Position;
};

[shader("vertex")]
VertexOutput vertMain(uint vertexID: SV_VertexID, uint instanceID: SV_VulkanInstanceID) {
    VertexOutput output;

    float2 uv = float2((vertexID << 1) & 2, vertexID & 2);
    float4 clipPos = float4(uv * 2.0f - 1.0f, 1.0f, 1.0f);
    output.sv_position = clipPos;

    float3x3 rotView = (float3x3)passData.view;
    float4 target = mul(passData.invProj, clipPos);
    float3 viewSpaceRay = target.xyz / target.w;
    output.viewRay = mul(transpose(rotView), viewSpaceRay);

    return output;
}

[shader("fragment")]
float4 fragMain(VertexOutput inVert) : SV_Target {
    float3 dir = normalize(inVert.viewRay);
    return cubemapArray[matParams.textureIndex].Sample(globalSampler, dir);
}