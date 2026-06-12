struct GlobalUBO {
    float4x4 view;
    float4x4 proj;
    float4x4 invProj; 
    float    time;
};

struct ObjectData {
    float4x4 transform;
    uint textureID;
    uint pad[3];
};

[[vk::binding(0, 0)]] ConstantBuffer<GlobalUBO>    globalUBO;
[[vk::binding(1, 0)]] StructuredBuffer<ObjectData> objectData;
[[vk::binding(2, 0)]] TextureCube                  cubemapArray[];
[[vk::binding(3, 0)]] SamplerState                 globalSampler;

struct VertexOutput {
    float3 viewRay : TEXCOORD0;
    nointerpolation uint objectIndex : TEXCOORD1;
    float4 sv_position : SV_Position;
};

[shader("vertex")]
VertexOutput vertMain(uint vertexID : SV_VertexID, uint instanceID : SV_VulkanInstanceID) {
    VertexOutput output;
    output.objectIndex = instanceID;

    float2 uv       = float2((vertexID << 1) & 2, vertexID & 2);
    float4 clipPos  = float4(uv * 2.0f - 1.0f, 1.0f, 1.0f);
    output.sv_position = clipPos;

    float3x3 rotView = (float3x3)globalUBO.view;
    float4 target = mul(globalUBO.invProj, clipPos);
    float3 viewSpaceRay = target.xyz / target.w;
    output.viewRay = mul(transpose(rotView), viewSpaceRay);

    return output;
}

[shader("fragment")]
float4 fragMain(VertexOutput inVert) : SV_Target {
    uint texID = objectData[inVert.objectIndex].textureID;
    float3 dir = normalize(inVert.viewRay);

    float3 color = cubemapArray[NonUniformResourceIndex(texID)].SampleLevel(globalSampler, dir, 0.0).rgb;

    return float4(color, 1.0);
}