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
[[vk::binding(2, 0)]] Texture2D                    textureArray[];
[[vk::binding(3, 0)]] SamplerState                 globalSampler;

struct VertexInput {
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float3 color    : COLOR;
    [[vk::location(2)]] float3 normal   : NORMAL;
    [[vk::location(3)]] float2 uv       : TEXCOORD0;
};

struct VertexOutput {
    float3 color  : COLOR;
    float2 uv     : TEXCOORD0;
    float3 normal : TEXCOORD1;
    nointerpolation uint objectIndex : TEXCOORD2;
    float4 sv_position : SV_Position;
};

[shader("vertex")]
VertexOutput vertMain(VertexInput vin, uint instanceID : SV_VulkanInstanceID) {
    VertexOutput output;

    float4x4 model  = objectData[instanceID].transform;
    float4 worldPos = mul(model, float4(vin.position, 1.0));

    output.sv_position = mul(globalUBO.proj, mul(globalUBO.view, worldPos));
    output.color       = vin.color;
    output.uv          = float2(1.0 - vin.uv.x, vin.uv.y);
    output.normal      = normalize(mul((float3x3)model, vin.normal));
    output.objectIndex = instanceID;

    return output;
}

[shader("fragment")]
float4 fragMain(VertexOutput inVert) : SV_Target {
    uint texID      = objectData[inVert.objectIndex].textureID;
    float4 texColor = textureArray[NonUniformResourceIndex(texID)].Sample(globalSampler, inVert.uv);

    float3 lightDir = normalize(float3(-0.5, 1.0, -0.5));
    float diffuse   = max(dot(normalize(inVert.normal), lightDir), 0.2);

    return texColor * float4(inVert.color, 1.0) * diffuse;
}
