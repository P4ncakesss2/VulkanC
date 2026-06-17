struct GlobalUBO {
    float time;
};
[[vk::binding(0, 0)]] ConstantBuffer<GlobalUBO> globalUBO;

struct GeometryPassData {
    float4x4 proj;
    float4x4 view;
    float4x4 invProj;
    float4   cameraPos;
    uint irradianceMapIndex;
    uint radianceMapIndex;
    uint brdfLutIndex;
    uint radianceMipCount;
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
[[vk::binding(3, 0)]] TextureCube cubemapArray[];

[[vk::binding(4, 0)]] SamplerState linearSampler;
[[vk::binding(5, 0)]] SamplerState nearestSampler;

struct MaterialParams {
    uint colorMapIndex;
    uint normalMapIndex;
    uint roughnessMapIndex;
    uint metallicMapIndex;
    uint heightMapIndex;
};
[[vk::binding(0, 2)]] ConstantBuffer<MaterialParams> matParams;

struct VertexInput {
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float3 color    : COLOR;
    [[vk::location(2)]] float3 normal   : NORMAL;
    [[vk::location(3)]] float2 uv       : TEXCOORD0;
    [[vk::location(4)]] float4 tangent  : TANGENT;
};

struct VertexOutput {
    float3 color : COLOR;
    float2 uv : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float3 tangent : TANGENT0;
    float3 bitangent : TANGENT1;
    float3 worldPos : POSITION0;
    float4 sv_position : SV_Position;
};
float3 fresnelSchlickRoughness(float cosTheta, float3 F0, float roughness) {
    return F0 + (max(float3(1.0 - roughness, 1.0 - roughness, 1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

[shader("vertex")]
VertexOutput vertMain(VertexInput vin, uint instanceID: SV_VulkanInstanceID) {
    VertexOutput output;

    float4x4 model = objectData[instanceID].transform;
    float4 worldPos = mul(model, float4(vin.position, 1.0));
    
    output.sv_position = mul(passData.proj, mul(passData.view, worldPos));
    output.worldPos = worldPos.xyz;
    output.color = vin.color;
    output.uv = vin.uv;

    float3 worldNormal  = normalize(mul((float3x3)model, vin.normal));
    float3 worldTangent = normalize(mul((float3x3)model, vin.tangent.xyz));
    float3 worldBitangent = cross(worldNormal, worldTangent) * vin.tangent.w;

    output.normal    = worldNormal;
    output.tangent   = worldTangent;
    output.bitangent = worldBitangent;

    return output;
}

[shader("fragment")]
float4 fragMain(VertexOutput inVert) : SV_Target {
    float3 normal    = normalize(inVert.normal);
    float3 tangent   = normalize(inVert.tangent);
    float3 bitangent = normalize(inVert.bitangent);
    float3x3 TBN     = float3x3(tangent, bitangent, normal);

    float3 normalMapSample = textureArray[matParams.normalMapIndex].Sample(linearSampler, inVert.uv).rgb;
    float3 localNormal = normalMapSample * 2.0 - 1.0;
    float3 N = normalize(mul(localNormal, TBN));

    float3 V = normalize(passData.cameraPos.xyz - inVert.worldPos);
    float3 R = reflect(-V, N);
    float NdotV = max(dot(N, V), 0.0);

    float4 texColor = textureArray[matParams.colorMapIndex].Sample(linearSampler, inVert.uv);
    float3 albedo   = texColor.rgb * inVert.color;

    float roughness = textureArray[matParams.roughnessMapIndex].Sample(linearSampler, inVert.uv).g;
    float metallic = textureArray[matParams.metallicMapIndex].Sample(linearSampler, inVert.uv).b;
    float3 F0 = float3(0.04, 0.04, 0.04); 
    F0 = lerp(F0, albedo, metallic);

    float3 F = fresnelSchlickRoughness(NdotV, F0, roughness);

    float3 kS = F;
    float3 kD = 1.0 - kS;
    kD *= 1.0 - metallic;

    float3 irradiance = cubemapArray[passData.irradianceMapIndex].Sample(linearSampler, N).rgb;
    float3 diffuseIBL = irradiance * albedo;

    float lod = roughness * (float)(passData.radianceMipCount - 1);
    float3 prefilteredColor = cubemapArray[passData.radianceMapIndex].SampleLevel(linearSampler, R, lod).rgb;

    float2 envBRDF = textureArray[passData.brdfLutIndex].Sample(nearestSampler, float2(NdotV, roughness)).rg;
    float3 specularIBL = prefilteredColor * (F * envBRDF.x + envBRDF.y);

    float3 ambient = (kD * diffuseIBL) + specularIBL;

    float3 lightDir = normalize(float3(-0.5, 1.0, -0.5));
    float diff      = max(dot(N, lightDir), 0.0);
    float3 direct   = kD * albedo * diff; 

    float3 finalColor = ambient + direct;

    return float4(finalColor, texColor.a);
}