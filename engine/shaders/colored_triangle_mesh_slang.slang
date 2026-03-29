struct CameraData
{
    float4x4 view;
    float4x4 projection;
    float4x4 viewProjection;
    float4   worldPosition;
    float4x4 lightViewProj;
};

struct PushConstants
{
    float4x4 modelMatrix;
};

[[vk::binding(2, 0)]] ConstantBuffer<CameraData>  cam;
[[vk::push_constant]]  ConstantBuffer<PushConstants> pc;

struct VSInput
{
    float3 position : POSITION;
    float2 uv       : TEXCOORD;
    float3 normal   : NORMAL;
    float4 color    : COLOR;
    float4 tangent  : TANGENT;
};

struct VSOutput
{
    float4 sv_pos      : SV_Position;
    float3 worldPos    : WORLD_POS;
    float2 uv          : TEXCOORD;
    float3 normal      : NORMAL;
    float4 color       : COLOR;
    float4 tangent     : TANGENT;

    // ── NEW: light-space position passed straight to fragment ──
    // Avoids recomputing lightViewProj * worldPos in every fragment,
    // and gives the interpolated position for free from the rasterizer.
    float4 lightSpacePos : LIGHT_SPACE_POS;
};

[shader("vertex")]
VSOutput vertexMain(VSInput input)
{
    VSOutput output;

    float4 worldPos  = mul(pc.modelMatrix, float4(input.position, 1.0));
    output.worldPos  = worldPos.xyz;
    output.uv        = input.uv;
    output.color     = input.color;

    // Normal to world space — use inverse-transpose for non-uniform scale correctness
    float3x3 normalMatrix = transpose(((float3x3)pc.modelMatrix));  // approximate; fine for uniform scale
    output.normal    = normalize(mul(normalMatrix, input.normal));

    // Tangent to world space, preserve handedness sign
    float3 worldTangent = normalize(mul((float3x3)pc.modelMatrix, input.tangent.xyz));
    output.tangent      = float4(worldTangent, input.tangent.w);

    // Clip position
    output.sv_pos = mul(cam.viewProjection, worldPos);

    // Light-space position for shadow lookup (perspective divide happens in fragment)
    output.lightSpacePos = mul(cam.lightViewProj, worldPos);

    return output;
}
