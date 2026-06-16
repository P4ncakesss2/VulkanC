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
    uint indexCount;
    uint firstIndex;
    int vertexOffset;
    uint pad1;
};
[[vk::binding(1, 0)]] StructuredBuffer<ObjectData> objectData;

[[vk::binding(2, 0)]] Texture2D textureArray[];
[[vk::binding(2, 0)]] TextureCube cubemapArray[];

[[vk::binding(3, 0)]] SamplerState globalSampler;

struct MaterialParams {
    uint textureIndex; 
};
[[vk::binding(0, 2)]] ConstantBuffer<MaterialParams> matParams;

struct VertexInput {
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float3 color    : COLOR;
    [[vk::location(2)]] float3 normal   : NORMAL;
    [[vk::location(3)]] float2 uv       : TEXCOORD0;
};

struct VertexOutput {
    float3 color : COLOR; 
    float2 uv    : TEXCOORD0; 
    float3 normal : TEXCOORD1; 
    float4 sv_position : SV_Position;
};

[shader("vertex")]
VertexOutput vertMain(VertexInput vin, uint instanceID: SV_VulkanInstanceID) {
    VertexOutput output;

    float4x4 model = objectData[instanceID].transform;
    float4 worldPos = mul(model, float4(vin.position, 1.0));
    output.sv_position = mul(passData.proj, mul(passData.view, worldPos));
    output.color = vin.color;
    output.normal = normalize(mul((float3x3)model, vin.normal));
    output.uv = vin.uv;

    return output;
}

[shader("fragment")]
float4 fragMain(VertexOutput inVert) : SV_Target {
    float3 lightDir = normalize(float3(-0.5, 1.0, -0.5));
    float diffuse   = max(dot(normalize(inVert.normal), lightDir), 0.2);

    float4 texColor = textureArray[matParams.textureIndex].Sample(globalSampler, inVert.uv);
    float3 finalColor = (texColor.rgb * inVert.color * diffuse);

    return float4(finalColor, texColor.a);
}