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

const float PI      = 3.14159265359;
const float INV_PI  = 0.31830988618;
const float MAX_REFLECTION_LOD = 4.0;

// Clear coat parameters — tweak to taste for the helmet's visor/shell
const float CC_STRENGTH   = 0.65;  // how much of a protective coating
const float CC_ROUGHNESS  = 0.04;  // very glassy surface on top

// ============================================================================
// NOISE / HASH (for shadow sampling rotation)
// ============================================================================

float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453123);
}

// ============================================================================
// SHADOWS — 16-tap Poisson PCF, random rotation per fragment
//
// Removes the blocky banding of a fixed grid sample. The random rotation is
// per-fragment (not per-frame) so it stays stable across frames, but the
// Poisson pattern covers the penumbra evenly.
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

    // Rotate the Poisson disk by a random angle per fragment — breaks banding
    float angle = hash(gl_FragCoord.xy) * 2.0 * PI;
    float sa = sin(angle), ca = cos(angle);
    mat2  rot = mat2(ca, sa, -sa, ca);

    const float filterRadius = 2.5; // world-space softness; increase for softer penumbra
    for (int i = 0; i < 16; i++) {
        vec2 offset       = rot * POISSON_DISK[i] * texelSize * filterRadius;
        float sampledDepth = texture(allTextures[nonuniformEXT(pc.shadowMapIndex)], proj.xy + offset).r;
        shadow += (currentDepth < sampledDepth) ? 1.0 : 0.0;
    }
    return shadow / 16.0;
}

// ============================================================================
// TONEMAPPING — AgX (Blender / Unity 2023 standard)
//
// Why not ACES? ACES clips saturated channels on colored metals (copper, gold)
// and causes a hue shift at high exposure. AgX applies a per-channel sigmoid
// that preserves hue even when channels are wildly different in magnitude.
// The result: gold stays gold at high intensity instead of washing white.
// ============================================================================

vec3 agxDefaultContrastApprox(vec3 x) {
    // Fitted 6th-order polynomial approximation of the AgX sigmoid curve
    vec3 x2 = x * x;
    vec3 x4 = x2 * x2;
    return + 15.5    * x4 * x2
           - 40.14   * x4 * x
           + 31.96   * x4
           - 6.868   * x2 * x
           + 0.4298  * x2
           + 0.1191  * x
           - 0.00232;
}

vec3 AgX(vec3 color) {
    // Linear sRGB -> AgX Log (inset matrix from the spec)
    const mat3 AgXInsetMatrix = mat3(
        0.842479062253094,  0.0784335999999992, 0.0792237451477643,
        0.0423282422610123, 0.878468636469772,  0.0791661274605434,
        0.0423756549057051, 0.0784336,          0.879142973793104
    );
    color = AgXInsetMatrix * color;
    color = max(color, 1e-10);
    color = log2(color);
    // Normalise into [0,1] from the AgX scene-linear range [-12.47393, 4.026069]
    color = (color - (-12.47393)) / (4.026069 - (-12.47393));
    color = clamp(color, 0.0, 1.0);
    return agxDefaultContrastApprox(color);
}

vec3 AgXEotf(vec3 color) {
    // Apply the outset matrix (inverse of inset) and linearise
    const mat3 AgXOutsetMatrix = mat3(
         1.19687900512017,   -0.0980208811401368, -0.0990297440797205,
        -0.0528968517574562,  1.15190312990417,   -0.0989611768448433,
        -0.0529716355144438, -0.0980434501171241,  1.15107367264116
    );
    color = pow(max(color, vec3(0.0)), vec3(2.2));
    return max(AgXOutsetMatrix * color, vec3(0.0));
}

// ============================================================================
// PBR — GEOMETRIC SPECULAR ANTI-ALIASING (Tokuyoshi & Kaplanyan 2019)
//
// Normal maps create high-frequency normals. When rasterised at less than
// infinite resolution those high frequencies alias into specular shimmer.
// This function measures how fast the normal is changing across the pixel
// (via ddx/ddy of the shading normal) and widens roughness proportionally.
// The kernel clamped to 0.18 prevents over-blurring flat surfaces.
// ============================================================================

float geometricSpecularAA(vec3 N, float roughness) {
    vec3  dndu      = dFdx(N);
    vec3  dndv      = dFdy(N);
    float variance  = 0.5 * (dot(dndu, dndu) + dot(dndv, dndv));
    float kRough2   = min(2.0 * variance, 0.18);
    return sqrt(clamp(roughness * roughness + kRough2, 0.0, 1.0));
}

// ============================================================================
// PBR CORE — GGX microfacet model
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

// Standard Fresnel — used for the base layer and clear coat base
vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// Chromatic Fresnel — simulates light dispersion at glancing angles
//
// Real glass and metal have slightly different Fresnel curves per wavelength.
// We shift F90 towards warm-red in R and cool-blue in B by ~5% — subtle but
// gives high-gloss metal that distinctive iridescent edge catch you see on
// real painted helmets and lacquered surfaces.
vec3 fresnelSchlickChromatic(float cosTheta, vec3 F0) {
    float f    = pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
    vec3  F90  = mix(vec3(1.0), vec3(1.05, 1.0, 0.95), 0.35);
    return F0 + (F90 - F0) * f;
}

// Fresnel for IBL (roughness-aware, no chromatic shift — not needed at this scale)
vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness) {
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// ============================================================================
// OCCLUSION
//
// specularOcclusion: Lagarde 2014.
//   Plain AO attenuates diffuse well, but applying it directly to specular
//   is physically wrong — AO measures the hemisphere, specular lives in a
//   tight lobe. This function accounts for lobe angle (roughness) and the
//   NdotV-based self-occlusion at grazing angles.
//
// horizonOcclusion: prevents IBL reflections from sampling geometry that
//   should be below the surface horizon. Cheap and surprisingly effective
//   on concave forms like the inside of a helmet visor.
// ============================================================================

float specularOcclusion(float NdotV, float ao, float roughness) {
    return clamp(pow(NdotV + ao, exp2(-16.0 * roughness - 1.0)) - 1.0 + ao, 0.0, 1.0);
}

float horizonOcclusion(vec3 R, vec3 Ng) {
    // Smooth ramp that zeroes out reflections pointing below the geometry normal
    float h = dot(R, Ng);
    return clamp(1.0 + 1.8 * h, 0.0, 1.0);
}

// ============================================================================
// CLEAR COAT — physically-based second BRDF lobe (KHR_materials_clearcoat)
//
// The damaged helmet has a protective lacquer/clearcoat. This is modelled as
// a second specular lobe sitting on top of the base PBR layer:
//
//   finalColor = baseLayer * (1 - F_coat * strength) + coatSpecular
//
// The coat uses IOR=1.5 (F0=0.04), the same as an air/glass interface.
// Its roughness is kept very low so the visor shows tight, sharp reflections.
// The base layer attenuation physically represents the coat absorbing light
// that would otherwise reach the base metallic layer.
// ============================================================================

vec3 evalClearCoatSpecular(vec3 N, vec3 V, vec3 L, vec3 H) {
    float NdotH = max(dot(N, H), 0.0);
    float VdotH = max(dot(V, H), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 0.0001);

    float a2    = CC_ROUGHNESS * CC_ROUGHNESS * CC_ROUGHNESS * CC_ROUGHNESS;
    float D_cc  = DistributionGGX(NdotH, a2);
    float G_cc  = GeometrySmith(NdotV, NdotL, CC_ROUGHNESS);
    vec3  F_cc  = fresnelSchlick(VdotH, vec3(0.04));

    return (D_cc * G_cc * F_cc) / (4.0 * NdotV * NdotL + 0.0001) * CC_STRENGTH;
}

// Attenuation applied to base layer from the coat's Fresnel (energy conservation)
vec3 clearCoatBaseAttenuation(float VdotH) {
    vec3 F_cc = fresnelSchlick(VdotH, vec3(0.04));
    return vec3(1.0) - F_cc * CC_STRENGTH;
}

// ============================================================================
// MAIN
// ============================================================================

void main() {

    // ── 1. MATERIAL SAMPLING ────────────────────────────────────────────────
    vec4 albedoSample = (pc.albedoIdx != 0u)
        ? texture(allTextures[nonuniformEXT(pc.albedoIdx)], inUV)
        : vec4(1.0);

    // Textures are sRGB on disk; PBR math must work in linear light
    vec3  albedo = pow(albedoSample.rgb, vec3(2.2)) * pc.colorFactor.rgb;
    float alpha  = albedoSample.a * pc.colorFactor.a;
    if (alpha < 0.1) discard;

    float roughness = pc.roughnessFactor;
    float metallic  = pc.metallicFactor;
    if (pc.metalRoughIdx != 0u) {
        // GLTF 2.0 spec: G channel = roughness, B channel = metallic
        vec2 mr = texture(allTextures[nonuniformEXT(pc.metalRoughIdx)], inUV).gb;
        roughness *= mr.x;
        metallic  *= mr.y;
    }
    roughness = clamp(roughness, 0.04, 1.0);

    // ── 2. NORMALS ──────────────────────────────────────────────────────────
    vec3 Ng = normalize(inNormal);   // geometric (vertex) normal — used for horizon occlusion
    vec3 N  = Ng;
    vec3 T  = normalize(inTangent.xyz);
    vec3 B  = cross(Ng, T) * inTangent.w;  // sign from tangent.w handles mirrored UVs

    if (pc.normalIdx != 0u) {
        vec3 nm = texture(allTextures[nonuniformEXT(pc.normalIdx)], inUV).xyz * 2.0 - 1.0;
        nm.xy  *= pc.normalStrength;
        N = normalize(mat3(T, B, Ng) * normalize(nm));
    }

    // Geometric Specular AA: widen roughness where the normal map has
    // high-frequency detail to prevent specular aliasing / shimmer
    roughness = geometricSpecularAA(N, roughness);

    float a  = roughness * roughness;          // perceptual -> linear roughness
    float a2 = a * a;                          // GGX alpha^2

    // ── 3. VIEW / REFLECTION VECTORS ────────────────────────────────────────
    vec3  V     = normalize(cam.worldPosition.xyz - inWorldPos);
    vec3  R     = reflect(-V, N);
    float NdotV = max(dot(N, V), 0.0001);

    vec3 F0 = mix(vec3(0.04), albedo, metallic);  // dielectric 4% reflectance, or albedo-tinted for metals

    // ── 4. OCCLUSION TERMS ──────────────────────────────────────────────────
    float ao = (pc.aoIdx != 0u)
        ? texture(allTextures[nonuniformEXT(pc.aoIdx)], inUV).r
        : 1.0;

    float specOcc  = specularOcclusion(NdotV, ao, roughness);
    float horizOcc = horizonOcclusion(R, Ng);

    // ── 5. DIRECT LIGHTING ──────────────────────────────────────────────────
    vec3  L     = normalize(pc.sunDirection);
    vec3  H     = normalize(V + L);
    float NdotL = max(dot(N, L), 0.0);
    float NdotH = max(dot(N, H), 0.0);
    float VdotH = max(dot(V, H), 0.0);

    float D = DistributionGGX(NdotH, a2);
    float G = GeometrySmith(NdotV, NdotL, roughness);
    vec3  F = fresnelSchlickChromatic(VdotH, F0);  // chromatic dispersion at grazing

    vec3 kD       = (1.0 - F) * (1.0 - metallic);  // metals have no diffuse
    vec3 specular = (D * G * F) / (4.0 * NdotV * NdotL + 0.0001);

    float shadow   = calcShadow(inWorldPos, pc.shadowBias);
    vec3  radiance = pc.sunColor * pc.sunIntensity * NdotL * shadow;

    // Clear coat direct contribution
    vec3 ccSpec      = evalClearCoatSpecular(N, V, L, H);
    vec3 ccBaseAtten = clearCoatBaseAttenuation(VdotH);

    // Base layer attenuated by the coat sitting above it, then coat on top
    vec3 directLight = ((kD * albedo * INV_PI + specular) * ccBaseAtten + ccSpec) * radiance;

    // ── 6. IMAGE-BASED LIGHTING (IBL) ───────────────────────────────────────

    // Sample the precomputed BRDF LUT
    vec3 F_ibl   = fresnelSchlickRoughness(NdotV, F0, roughness);
    vec2 envBRDF = texture(allTextures[nonuniformEXT(pc.iblBrdfLutIndex)], vec2(NdotV, roughness)).rg;

    // ── Multi-scatter energy compensation (Turquin 2019 / Fdez-Agüera 2019) ──
    //
    // Standard split-sum IBL only accounts for single scattering events.
    // Multiple bounces between microfacets are lost, making rough metals look
    // darker than they should. This model adds back the lost energy:
    //
    //   FssEss  = single-scatter contribution  (what the LUT gives us)
    //   Ems     = energy lost (1 - single-scatter)
    //   Favg    = average Fresnel over the hemisphere
    //   Fms     = multi-scatter boost that re-injects Ems back into the result
    //
    vec3  FssEss    = F_ibl * envBRDF.x + envBRDF.y;
    float Ess       = envBRDF.x + envBRDF.y;
    float Ems       = 1.0 - Ess;
    vec3  Favg      = F0 + (1.0 - F0) / 21.0;   // closed-form average Fresnel
    vec3  Fms       = FssEss * Favg / (1.0 - (1.0 - Ess) * Favg);
    vec3  multiComp = FssEss + Fms * Ems;         // single + multi = correct total

    // Diffuse IBL: irradiance cubemap * albedo (metals have no diffuse)
    vec3 irradiance  = texture(allCubemaps[nonuniformEXT(pc.iblIrradianceIndex)], N).rgb;
    vec3 diffuseIBL  = irradiance * albedo * (1.0 - metallic) * ao;

    // Specular IBL: prefiltered env at the correct roughness mip
    vec3 prefilteredColor = textureLod(
        allCubemaps[nonuniformEXT(pc.iblPrefilterIndex)], R,
        roughness * MAX_REFLECTION_LOD).rgb;

    vec3 specularIBL = prefilteredColor * multiComp * specOcc * horizOcc;

    // Clear coat IBL: sample a very-low-roughness mip for the glossy coat
    float ccMip     = CC_ROUGHNESS * MAX_REFLECTION_LOD;
    vec3  ccEnv     = textureLod(allCubemaps[nonuniformEXT(pc.iblPrefilterIndex)], R, ccMip).rgb;
    vec3  F_cc_ibl  = fresnelSchlickRoughness(NdotV, vec3(0.04), CC_ROUGHNESS);
    vec3  ccIBL     = ccEnv * F_cc_ibl * CC_STRENGTH * horizOcc * specOcc;

    vec3 ambient = diffuseIBL + specularIBL + ccIBL;

    // ── 7. EMISSIVE ─────────────────────────────────────────────────────────
    vec3 emissive = vec3(0.0);
    if (pc.emissiveIdx != 0u) {
        // Helmet emissive map covers burn marks, glowing wires, HUD elements.
        // Linearise from sRGB then push hard — these should blow out to HDR
        // and let the tonemapper decide how they clip into the display range.
        vec3 emSample = pow(texture(allTextures[nonuniformEXT(pc.emissiveIdx)], inUV).rgb, vec3(2.2));
        emissive = emSample * 12.0;
    }

    // ── 8. COMPOSITE ────────────────────────────────────────────────────────
    vec3 color = directLight + ambient + emissive;

    // AgX tonemapping — handles wide HDR gamut without hue shift or clipping.
    // Forward pass maps scene linear -> display linear via an S-curve sigmoid.
    // Eotf pass applies the inverse matrix and re-linearises before sRGB output.
    color = AgX(color);
    color = AgXEotf(color);

    // Gamma encode for sRGB display (monitor expects 2.2 power-law)
    color = pow(max(color, vec3(0.0)), vec3(1.0 / 2.2));

    outColor = vec4(color, alpha);
}
