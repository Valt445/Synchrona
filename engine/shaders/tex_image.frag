#version 460

#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_scalar_block_layout  : require

layout(location = 0) in vec3 inWorldPos;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec3 inNormal;
layout(location = 3) in vec4 inColor;
layout(location = 4) in vec4 inTangent;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D   allTextures[];
layout(set = 0, binding = 3) uniform samplerCube allCubemaps[];

layout(set = 0, binding = 2) uniform CameraData {
    mat4 view;
    mat4 projection;
    mat4 viewProjection;
    vec4 worldPosition;
    mat4 lightViewProj;
} cam;

layout(scalar, push_constant) uniform constants {
    mat4  modelMatrix;
    uint  albedoIdx;
    uint  normalIdx;
    uint  metalRoughIdx;
    uint  aoIdx;
    uint  emissiveIdx;
    float metallicFactor;
    float roughnessFactor;
    float normalStrength;
    vec4  colorFactor;
    vec3  sunDirection;
    vec3  sunColor;
    float sunIntensity;
    uint  shadowMapIndex;
    float shadowBias;
    uint  iblIrradianceIndex;
    uint  iblPrefilterIndex;
    uint  iblBrdfLutIndex;
} pc;

const float PI               = 3.14159265359;
const float INV_PI           = 0.31830988618;
const float MAX_REFLECTION_LOD = 4.0;

// ============================================================================
// SPONZA CALIBRATION
// ============================================================================

const float SHADOW_MIN_DIFFUSE  = 0.18;
const float SHADOW_MIN_SPECULAR = 0.06;

const vec3  SKY_FILL_COLOR      = vec3(0.55, 0.72, 1.00);
const float SKY_FILL_INTENSITY  = 0.10;

const vec3  GROUND_BOUNCE_COLOR     = vec3(1.00, 0.88, 0.60);
const float GROUND_BOUNCE_INTENSITY = 0.06;

const vec3  SUN_WARM_TINT    = vec3(1.06, 1.00, 0.93);
const float SPECULAR_SCALE   = 0.68;
const float EMISSIVE_SCALE   = 7.0;
const float CHROMATIC_STRENGTH = 0.08;
const float SHADOW_FILTER_RADIUS = 4.0;

// ============================================================================
// NOISE
// ============================================================================

float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453123);
}

// ============================================================================
// SHADOWS — 16-tap Poisson PCF
// ============================================================================

const vec2 POISSON_DISK[16] = vec2[16](
    vec2(-0.94201624, -0.39906216), vec2( 0.94558609, -0.76890725),
    vec2(-0.09418410, -0.92938870), vec2( 0.34495938,  0.29387760),
    vec2(-0.91588581,  0.45771432), vec2(-0.81544232, -0.87912464),
    vec2(-0.38277543,  0.27676845), vec2( 0.97484398,  0.75648379),
    vec2( 0.44323325, -0.97511554), vec2( 0.53742981, -0.47373420),
    vec2(-0.26496911, -0.41893023), vec2( 0.79197514,  0.19090188),
    vec2(-0.24188840,  0.99706507), vec2(-0.81409955,  0.91437590),
    vec2( 0.19984126,  0.78641367), vec2( 0.14383161, -0.14100790)
);

float calcShadow(vec3 worldPos, float bias) {
    vec4 lightSpace = cam.lightViewProj * vec4(worldPos, 1.0);
    vec3 proj       = lightSpace.xyz / lightSpace.w;
    proj.xy         = proj.xy * 0.5 + 0.5;

    if (proj.z > 1.0 || any(lessThan(proj.xy, vec2(0.0))) || any(greaterThan(proj.xy, vec2(1.0))))
        return 1.0;

    float currentDepth = proj.z - bias;
    float shadow       = 0.0;
    vec2  texelSize    = 1.0 / vec2(textureSize(allTextures[nonuniformEXT(pc.shadowMapIndex)], 0));

    float angle = hash(gl_FragCoord.xy) * 2.0 * PI;
    float sa = sin(angle), ca = cos(angle);
    mat2  rot = mat2(ca, sa, -sa, ca);

    for (int i = 0; i < 16; i++) {
        vec2  offset       = rot * POISSON_DISK[i] * texelSize * SHADOW_FILTER_RADIUS;
        float sampledDepth = texture(allTextures[nonuniformEXT(pc.shadowMapIndex)], proj.xy + offset).r;
        shadow += (currentDepth < sampledDepth) ? 1.0 : 0.0;
    }
    return shadow / 16.0;
}

// ============================================================================
// TONEMAPPING — AgX
// ============================================================================

vec3 agxDefaultContrastApprox(vec3 x) {
    vec3 x2 = x * x;
    vec3 x4 = x2 * x2;
    return + 15.5   * x4 * x2
           - 40.14  * x4 * x
           + 31.96  * x4
           - 6.868  * x2 * x
           + 0.4298 * x2
           + 0.1191 * x
           - 0.00232;
}

vec3 AgX(vec3 color) {
    const mat3 AgXInsetMatrix = mat3(
        0.842479062253094,  0.0784335999999992, 0.0792237451477643,
        0.0423282422610123, 0.878468636469772,  0.0791661274605434,
        0.0423756549057051, 0.0784336,          0.879142973793104
    );
    color = AgXInsetMatrix * color;
    color = max(color, 1e-10);
    color = log2(color);
    color = (color - (-12.47393)) / (4.026069 - (-12.47393));
    color = clamp(color, 0.0, 1.0);
    return agxDefaultContrastApprox(color);
}

vec3 AgXEotf(vec3 color) {
    const mat3 AgXOutsetMatrix = mat3(
         1.19687900512017,   -0.0980208811401368, -0.0990297440797205,
        -0.0528968517574562,  1.15190312990417,   -0.0989611768448433,
        -0.0529716355144438, -0.0980434501171241,  1.15107367264116
    );
    color = pow(max(color, vec3(0.0)), vec3(2.2));
    return max(AgXOutsetMatrix * color, vec3(0.0));
}

// ============================================================================
// GEOMETRIC SPECULAR AA
// ============================================================================

float geometricSpecularAA(vec3 N, float roughness) {
    vec3  dndu    = dFdx(N);
    vec3  dndv    = dFdy(N);
    float variance = 0.5 * (dot(dndu, dndu) + dot(dndv, dndv));
    float kRough2  = min(2.0 * variance, 0.18);
    return sqrt(clamp(roughness * roughness + kRough2, 0.0, 1.0));
}

// ============================================================================
// PBR CORE
// ============================================================================

float DistributionGGX(float NdotH, float a2) {
    float f = (NdotH * a2 - NdotH) * NdotH + 1.0;
    return a2 / (PI * f * f + 1e-7);
}

float GeometrySchlickGGX(float NdotX, float k) {
    return NdotX / (NdotX * (1.0 - k) + k + 1e-7);
}

float GeometrySmith(float NdotV, float NdotL, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) * 0.125;
    return GeometrySchlickGGX(NdotV, k) * GeometrySchlickGGX(NdotL, k);
}

vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 fresnelSchlickChromatic(float cosTheta, vec3 F0) {
    float f   = pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
    vec3  F90 = mix(vec3(1.0), vec3(1.05, 1.0, 0.95), CHROMATIC_STRENGTH);
    return F0 + (F90 - F0) * f;
}

vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness) {
    return F0 + (max(vec3(1.0 - roughness), F0) - F0)
              * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// ============================================================================
// OCCLUSION
// ============================================================================

float specularOcclusion(float NdotV, float ao, float roughness) {
    return clamp(pow(NdotV + ao, exp2(-16.0 * roughness - 1.0)) - 1.0 + ao, 0.0, 1.0);
}

float horizonOcclusion(vec3 R, vec3 Ng) {
    return clamp(1.0 + 1.8 * dot(R, Ng), 0.0, 1.0);
}

// ============================================================================
// MAIN
// ============================================================================

void main() {

    // ── 1. MATERIAL SAMPLING ─────────────────────────────────────────────────

    vec4 albedoSample = (pc.albedoIdx != 0u)
        ? texture(allTextures[nonuniformEXT(pc.albedoIdx)], inUV)
        : vec4(1.0);

    // FIX: removed pow(x, 2.2) — textures are VK_FORMAT_R8G8B8A8_SRGB so
    // hardware already linearises them. pow() was double-converting.
    vec3  albedo = albedoSample.rgb * pc.colorFactor.rgb;
    float alpha  = albedoSample.a  * pc.colorFactor.a;
    if (alpha < 0.1) discard;

    float roughness = pc.roughnessFactor;
    float metallic  = pc.metallicFactor;
    if (pc.metalRoughIdx != 0u) {
        vec2 mr    = texture(allTextures[nonuniformEXT(pc.metalRoughIdx)], inUV).gb;
        roughness *= mr.x;  // G = roughness (glTF spec)
        metallic  *= mr.y;  // B = metallic  (glTF spec)
    }
    roughness = clamp(roughness, 0.04, 1.0);

    // ── 2. NORMALS ───────────────────────────────────────────────────────────

    vec3 Ng = normalize(inNormal);
    vec3 N  = Ng;
    vec3 T  = normalize(inTangent.xyz);
    vec3 B  = cross(Ng, T) * inTangent.w;

    if (pc.normalIdx != 0u) {
        vec3 nm = texture(allTextures[nonuniformEXT(pc.normalIdx)], inUV).xyz * 2.0 - 1.0;
        nm.xy  *= pc.normalStrength;
        N       = normalize(mat3(T, B, Ng) * normalize(nm));
    }

    roughness = geometricSpecularAA(N, roughness);

    float a  = roughness * roughness;
    float a2 = a * a;

    // ── 3. VIEW / REFLECTION ──────────────────────────────────────────────────

    vec3  V     = normalize(cam.worldPosition.xyz - inWorldPos);
    vec3  R     = reflect(-V, N);
    float NdotV = max(dot(N, V), 0.0001);

    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    // ── 4. OCCLUSION ──────────────────────────────────────────────────────────

    float ao      = (pc.aoIdx != 0u)
        ? texture(allTextures[nonuniformEXT(pc.aoIdx)], inUV).r
        : 1.0;
    float specOcc  = specularOcclusion(NdotV, ao, roughness);
    float horizOcc = horizonOcclusion(R, Ng);

    // ── 5. DIRECT LIGHTING ────────────────────────────────────────────────────

    vec3  L     = normalize(pc.sunDirection);
    vec3  H     = normalize(V + L);

    // FIX: NdotL must be applied to direct diffuse + specular.
    // Previously missing entirely — surfaces facing away from the sun
    // were receiving full lighting, which is physically wrong.
    float NdotL = max(dot(N, L), 0.0);
    float NdotH = max(dot(N, H), 0.0);
    float VdotH = max(dot(V, H), 0.0);

    float D = DistributionGGX(NdotH, a2);
    float G = GeometrySmith(NdotV, NdotL, roughness);
    vec3  F = fresnelSchlickChromatic(VdotH, F0);

    vec3 kD       = (1.0 - F) * (1.0 - metallic);
    vec3 diffuse  = kD * albedo * INV_PI;
    vec3 specular = (D * G * F) / (4.0 * NdotV * NdotL + 0.0001) * SPECULAR_SCALE;

    // Shadow
    float shadowRaw      = calcShadow(inWorldPos, pc.shadowBias);
    float shadowDiffuse  = max(shadowRaw, SHADOW_MIN_DIFFUSE);
    float shadowSpecular = max(shadowRaw, SHADOW_MIN_SPECULAR);

    vec3 sunRadiance = pc.sunColor * SUN_WARM_TINT * pc.sunIntensity;

    // Hemisphere fill lights (unaffected by NdotL — they are ambient wraps)
    float skyWrap    = dot(N, vec3(0.0, 1.0, 0.0)) * 0.5 + 0.5;
    vec3  skyFill    = SKY_FILL_COLOR * pc.sunIntensity * SKY_FILL_INTENSITY * skyWrap;

    float groundWrap = max(dot(N, vec3(0.0, -1.0, 0.0)) * 0.5 + 0.5, 0.0);
    vec3  groundFill = GROUND_BOUNCE_COLOR * pc.sunIntensity * GROUND_BOUNCE_INTENSITY * groundWrap;

    // FIX: NdotL now correctly attenuates both diffuse and specular direct terms
    vec3 directLight =
        (diffuse  * sunRadiance * NdotL * shadowDiffuse)
      + (specular * sunRadiance * NdotL * shadowSpecular)
      + ((skyFill + groundFill) * albedo * (1.0 - metallic));

    // ── 6. IBL ────────────────────────────────────────────────────────────────

    vec3 F_ibl   = fresnelSchlickRoughness(NdotV, F0, roughness);
    vec2 envBRDF = texture(allTextures[nonuniformEXT(pc.iblBrdfLutIndex)],
                           vec2(NdotV, roughness)).rg;

    vec3  FssEss    = F_ibl * envBRDF.x + envBRDF.y;
    float Ess       = envBRDF.x + envBRDF.y;
    float Ems       = 1.0 - Ess;
    vec3  Favg      = F0 + (1.0 - F0) / 21.0;
    vec3  Fms       = FssEss * Favg / (1.0 - (1.0 - Ess) * Favg);
    vec3  multiComp = FssEss + Fms * Ems;

    vec3 irradiance = texture(allCubemaps[nonuniformEXT(pc.iblIrradianceIndex)], N).rgb;
    vec3 diffuseIBL = irradiance * albedo * (1.0 - metallic) * ao;

    vec3 prefilteredColor = textureLod(
        allCubemaps[nonuniformEXT(pc.iblPrefilterIndex)], R,
        roughness * MAX_REFLECTION_LOD).rgb;
    vec3 specularIBL = prefilteredColor * multiComp * specOcc * horizOcc;

    // FIX: removed clear coat IBL block entirely (CC_STRENGTH was 0.0 —
    // it was sampling the prefilter cubemap and doing mat math for zero output)
    vec3 ambient = diffuseIBL + specularIBL;

    // ── 7. EMISSIVE ───────────────────────────────────────────────────────────

    vec3 emissive = vec3(0.0);
    if (pc.emissiveIdx != 0u) {
        // FIX: removed pow(x, 2.2) — emissive textures are also VK_FORMAT_R8G8B8A8_SRGB
        emissive = texture(allTextures[nonuniformEXT(pc.emissiveIdx)], inUV).rgb
                   * EMISSIVE_SCALE;
    }

    // ── 8. COMPOSITE & TONEMAP ────────────────────────────────────────────────

    vec3 color = directLight + ambient + emissive;

    color = AgX(color);
    color = AgXEotf(color);

    // Gentle warm lift for Sponza afternoon light
    color *= vec3(1.02, 1.00, 0.97);

    // FIX: AgXEotf outputs linear, encode to sRGB for the swapchain.
    // pow(1/2.2) is correct here — this is the only gamma encode in the file now.
    color = pow(max(color, vec3(0.0)), vec3(1.0 / 2.2));

    outColor = vec4(color, alpha);
}