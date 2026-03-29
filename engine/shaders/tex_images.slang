
// ============================================================================
// BINDINGS
// ============================================================================

Sampler2D allTextures[];  // bindless texture array

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
    uint     albedoIdx;
    uint     normalIdx;
    uint     metalRoughIdx;
    uint     aoIdx;
    uint     emissiveIdx;
    float    metallicFactor;
    float    roughnessFactor;
    float    normalStrength;
    float4   colorFactor;
    float3   sunDirection;
    float3   sunColor;
    float    sunIntensity;
    uint     shadowMapIndex;
    float    shadowBias;
};

[[vk::binding(0, 0)]] ParameterBlock<CameraData> cam;
[[vk::push_constant]]  ConstantBuffer<PushConstants> pc;

// ============================================================================
// VERTEX → FRAGMENT PAYLOAD
// ============================================================================

struct VSOutput
{
    float4 sv_pos    : SV_Position;
    float3 worldPos  : WORLD_POS;
    float2 uv        : TEXCOORD;
    float3 normal    : NORMAL;
    float4 color     : COLOR;
    float4 tangent   : TANGENT;
};

// ============================================================================
// CONSTANTS
// ============================================================================

static const float PI      = 3.14159265359;
static const float EPSILON = 0.0001;
static const float3 minLight = float3(0.001);

// ============================================================================
// PBR FUNCTIONS
// ============================================================================

float DistributionGGX(float NdotH, float roughness)
{
    float a  = roughness * roughness;
    float a2 = a * a;
    float d  = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d + EPSILON);
}

float GeometrySchlickGGX(float NdotX, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotX / (NdotX * (1.0 - k) + k + EPSILON);
}

float GeometrySmith(float NdotV, float NdotL, float roughness)
{
    return GeometrySchlickGGX(NdotV, roughness)
         * GeometrySchlickGGX(NdotL, roughness);
}

float3 fresnelSchlick(float cosTheta, float3 F0)
{
    float f = pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
    return F0 + (1.0 - F0) * f;
}

float3 fresnelSchlickRoughness(float cosTheta, float3 F0, float roughness)
{
    float f = pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
    return F0 + (max(float3(1.0 - roughness), F0) - F0) * f;
}

// ============================================================================
// SHADOW  (PCF 3×3)
// ============================================================================

float calcShadow(float3 worldPos, float bias)
{
    float4 lightSpace = mul(cam.lightViewProj, float4(worldPos, 1.0));
    float3 proj       = lightSpace.xyz / lightSpace.w;
    proj.xy           = proj.xy * 0.5 + 0.5;

    // Outside shadow frustum → fully lit
    if (proj.z > 1.0 || any(proj.xy < 0.0) || any(proj.xy > 1.0))
        return 1.0;

    float currentDepth = proj.z - bias;
    float2 texelSize   = 1.0 / float2(2048.0);
    float  shadow      = 0.0;

    [unroll]
    for (int x = -1; x <= 1; x++)
    {
        [unroll]
        for (int y = -1; y <= 1; y++)
        {
            float2 offset      = float2(x, y) * texelSize;
            float  sampledDepth = allTextures[NonUniformResourceIndex(pc.shadowMapIndex)]
                                    .Sample(proj.xy + offset).r;
            shadow += (currentDepth < sampledDepth) ? 1.0 : 0.0;
        }
    }
    return shadow / 9.0;
}

// ============================================================================
// ACES FILMIC TONE MAPPING
// ============================================================================

float3 ACESFilm(float3 x)
{
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

// ============================================================================
// FRAGMENT SHADER
// ============================================================================

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target
{
    // ── Albedo ────────────────────────────────────────────────────────────────
    float4 albedoSample = (pc.albedoIdx != 0u)
        ? allTextures[NonUniformResourceIndex(pc.albedoIdx)].Sample(input.uv)
        : float4(1.0);

    float3 albedo = albedoSample.rgb * input.color.rgb * pc.colorFactor.rgb;
    float  alpha  = albedoSample.a   * input.color.a   * pc.colorFactor.a;

    if (pc.albedoIdx != 0u && alpha < 0.7) discard;

    // ── Metallic / Roughness ──────────────────────────────────────────────────
    float roughness = pc.roughnessFactor;
    float metallic  = pc.metallicFactor;

    if (pc.metalRoughIdx != 0u)
    {
        float2 mr = allTextures[NonUniformResourceIndex(pc.metalRoughIdx)].Sample(input.uv).gb;
        roughness = clamp(mr.x * pc.roughnessFactor, 0.05, 1.0);
        metallic  = clamp(mr.y * pc.metallicFactor,  0.0,  1.0);
    }
    float roughnessP = roughness * roughness; // perceptual remap

    // ── AO ────────────────────────────────────────────────────────────────────
    float ao = (pc.aoIdx != 0u)
        ? allTextures[NonUniformResourceIndex(pc.aoIdx)].Sample(input.uv).r
        : 1.0;

    // ── Emissive ──────────────────────────────────────────────────────────────
    float3 emissive = float3(0.0);
    if (pc.emissiveIdx != 0u)
        emissive = allTextures[NonUniformResourceIndex(pc.emissiveIdx)].Sample(input.uv).rgb * 2.5;

    // ── Normal map ────────────────────────────────────────────────────────────
    float3 N = normalize(input.normal);

    if (pc.normalIdx != 0u)
    {
        float3 T = normalize(input.tangent.xyz);
        T = normalize(T - dot(T, N) * N);             // Gram-Schmidt re-orthogonalise
        float3 B = cross(N, T) * input.tangent.w;
        float3x3 TBN = float3x3(T, B, N);

        float3 nm = allTextures[NonUniformResourceIndex(pc.normalIdx)].Sample(input.uv).xyz * 2.0 - 1.0;
        nm.xy *= clamp(pc.normalStrength, 0.0, 1.0);
        N = normalize(mul(TBN, normalize(nm)));
    }

    // ── Lighting vectors ──────────────────────────────────────────────────────
    float3 V = normalize(cam.worldPosition.xyz - input.worldPos);
    float3 L = normalize(pc.sunDirection);
    float3 H = normalize(V + L);

    float NdotV    = max(dot(N, V), EPSILON);
    float NdotL    = max(dot(N, L), 0.0);
    float NdotL_hl = dot(N, L) * 0.5 + 0.5;          // half-lambert
    float NdotL_hl2 = NdotL_hl * NdotL_hl;
    float NdotH    = max(dot(N, H), 0.0);
    float HdotV    = max(dot(H, V), 0.0);

    // ── Cook-Torrance BRDF ────────────────────────────────────────────────────
    float3 F0 = lerp(float3(0.04), albedo, metallic);
    float3 F  = fresnelSchlick(HdotV, F0);
    float  D  = DistributionGGX(NdotH, roughnessP);
    float  G  = GeometrySmith(NdotV, NdotL, roughnessP);

    float3 specular = (D * G * F) / (4.0 * NdotV * NdotL + EPSILON);
    float3 kD       = (float3(1.0) - F) * (1.0 - metallic);
    float3 diffuse  = kD * albedo / PI;

    // ── Shadow ────────────────────────────────────────────────────────────────
    float shadowFactor = lerp(0.15, 1.0, calcShadow(input.worldPos, pc.shadowBias));

    // Diffuse uses squared half-lambert, specular uses hard NdotL
    float3 directLight = (diffuse * NdotL_hl2 + specular * NdotL)
                       * pc.sunColor * pc.sunIntensity;

    // ── Hemisphere ambient ────────────────────────────────────────────────────
    float3 skyColor    = float3(0.35, 0.45, 0.65);
    float3 groundColor = float3(0.20, 0.16, 0.10);
    float  hemi        = N.y * 0.5 + 0.5;
    float3 ambientIrr  = lerp(groundColor, skyColor, hemi);
    float3 kD_amb      = (float3(1.0) - fresnelSchlickRoughness(NdotV, F0, roughnessP)) * (1.0 - metallic);
    float3 ambient     = kD_amb * albedo * ambientIrr * ao * 1.5;
    float3 ambSpec     = skyColor * F0 * (1.0 - roughnessP) * ao * 0.3;

    // ── Composite ─────────────────────────────────────────────────────────────
    float3 color = (directLight * shadowFactor) + ambient + ambSpec + emissive;
    color = max(color, minLight);

    // ── Tone map + gamma ──────────────────────────────────────────────────────
    color = ACESFilm(color * 1.2);
    color = pow(color, float3(1.0 / 2.2));

    return float4(color, alpha);
}
