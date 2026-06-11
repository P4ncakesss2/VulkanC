struct GlobalUBO {
    float4x4 view;
    float4x4 proj;
    float    time;
};

struct ObjectData {
    float4x4 transform;
    uint textureID;
    uint pad[3];
};

[[vk::binding(0, 0)]] StructuredBuffer<ObjectData> objectData;
[[vk::binding(1, 0)]] Texture2D                    textureArray[];
[[vk::binding(2, 0)]] SamplerState                 globalSampler;
[[vk::binding(0, 1)]] ConstantBuffer<GlobalUBO>    globalUBO;

struct VertexInput {
    float3 position : POSITION;
    float3 color    : COLOR;
    float2 uv       : TEXCOORD0;
};

struct VertexOutput {
    float3 color : COLOR;
    float2 uv : TEXCOORD0;
    nointerpolation uint objectIndex : TEXCOORD1;
    float4 sv_position : SV_Position;
};

[shader("vertex")]
VertexOutput vertMain(VertexInput vin, uint instanceID: SV_VulkanInstanceID) {
    VertexOutput output;

    float4x4 model = objectData[instanceID].transform;
    float4 worldPos = mul(model, float4(vin.position, 1.0));

    output.sv_position = mul(globalUBO.proj, mul(globalUBO.view, worldPos));
    output.color = vin.color;
    output.uv = float2(1.0 - vin.uv.x, vin.uv.y);

    output.objectIndex = instanceID;
    return output;
}

[shader("fragment")]
float4 fragMain(VertexOutput inVert) : SV_Target {
    uint texID = objectData[inVert.objectIndex].textureID;
    float4 texColor = textureArray[NonUniformResourceIndex(texID)].Sample(globalSampler, inVert.uv);

    return texColor * float4(inVert.color, 1.0);
}