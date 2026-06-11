struct GlobalUBO {
    float4x4 view;
    float4x4 proj;
    float     time;
};

[[vk::binding(0, 0)]] StructuredBuffer<float4x4> objectMatrices;
[[vk::binding(0, 1)]] ConstantBuffer<GlobalUBO> globalUBO;

struct VertexInput {
    float3 position : POSITION;
    float3 color    : COLOR;
};

struct VertexOutput {
    float3 color    : COLOR;
    float4 sv_position : SV_Position;
};

[shader("vertex")]
VertexOutput vertMain(VertexInput vin, uint instanceID : SV_VulkanInstanceID) {
    VertexOutput output;
    float4x4 model = objectMatrices[instanceID];

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