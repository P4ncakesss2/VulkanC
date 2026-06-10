struct GlobalUBO {
    float4x4 view;
    float4x4 proj;
    float     time;
};

struct ObjectSSBO {
    float4x4 modelMatrices[1024];
};

[[vk::binding(0, 0)]] ConstantBuffer<GlobalUBO> globalUBO;
[[vk::binding(1, 0)]] StructuredBuffer<float4x4> modelMatrices;

struct PushConstants {
    uint objectIndex;
};
[[vk::push_constant]] PushConstants pushConstants;

struct VertexInput {
    float3 position : POSITION;
    float3 color    : COLOR;
};

struct VertexOutput {
    float3 color    : COLOR;
    float4 sv_position : SV_Position;
};

[shader("vertex")]
VertexOutput vertMain(VertexInput vin) {
    VertexOutput output;
    float4x4 model = modelMatrices[pushConstants.objectIndex];
    float4 worldPos = mul(model, float4(vin.position, 1.0));
    output.sv_position = mul(globalUBO.proj, mul(globalUBO.view, worldPos));
    output.color = vin.color;
    return output;
}

[shader("fragment")]
float4 fragMain(VertexOutput inVert) : SV_Target
{
    return float4(inVert.color, 1.0);
}