#version 460

#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_scalar_block_layout  : require


layout(location = 0) in vec3 inWorldPos;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec3 inNormal;
layout(location = 3) in vec4 inColor;
layout(location = 4) in vec4 inTangent;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D allTextures[];

layout(set = 0, binding = 2) uniform CameraData {
    mat4 view;
    mat4 projection;
    mat4 viewProjection;
    vec4 worldPosition;
    mat4 lightViewProj;   // ← new

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
} pc;

const float PI      = 3.14159265359;
const float EPSILON = 0.0001;
const vec3 minLight = vec3(0.001);  // prevent completely black areas (was missing)

// ============================================================================
// PBR FUNCTIONS
// ============================================================================

float DistributionGGX(float NdotH, float roughness) {
    float a  = roughness * roughness;
    float a2 = a * a;
    float d  = (NdotH * NdotH) * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d + EPSILON);
}

float GeometrySchlickGGX(float NdotX, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotX / (NdotX * (1.0 - k) + k + EPSILON);
}

float GeometrySmith(float NdotV, float NdotL, float roughness) {
    return GeometrySchlickGGX(NdotV, roughness)
         * GeometrySchlickGGX(NdotL, roughness);
}

vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    float f = pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
    return F0 + (1.0 - F0) * f;
}

vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness) {
    float f = pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * f;
}

float calcShadow(vec3 worldPos, float bias) {
    // Project world position into light clip space
    vec4 lightSpace = cam.lightViewProj * vec4(worldPos, 1.0);
    // Perspective divide — for ortho this is a no-op but good practice
    vec3 proj = lightSpace.xyz / lightSpace.w;
    // Remap from NDC [-1,1] to UV [0,1]
    proj.xy = proj.xy * 0.5 + 0.5;
    // Fragment is outside the shadow map — treat as lit
    if (proj.z > 1.0 || proj.x < 0.0 || proj.x > 1.0
                      || proj.y < 0.0 || proj.y > 1.0)
        return 1.0;

    float currentDepth = proj.z - bias; // subtract bias to prevent acne

    // PCF — 3x3 kernel using manual depth comparison (fully Vulkan-compliant)
    float shadow = 0.0;
    vec2 texelSize = 1.0 / vec2(2048.0); // must match your shadow map resolution
    for (int x = -1; x <= 1; x++) {
        for (int y = -1; y <= 1; y++) {
            vec2 offset = vec2(x, y) * texelSize;
            float sampledDepth = texture(
                allTextures[nonuniformEXT(pc.shadowMapIndex)], 
                proj.xy + offset
            ).r;

            shadow += (currentDepth < sampledDepth) ? 1.0 : 0.0;
        }
    }
    return shadow / 9.0; // average — gives soft PCF edges
}

// ============================================================================
// ACES FILMIC TONE MAPPING
// ============================================================================

vec3 ACESFilm(vec3 x) {
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

// ============================================================================
// MAIN
// ============================================================================

void main() {
    // -------------------------------------------------------------------------
    // ALBEDO
    // Multiply texture × vertex color × colorFactor so all three work together.
    // Vertex color defaults to vec4(1) in the loader, so meshes without vertex
    // colors are unaffected.
    // -------------------------------------------------------------------------
    vec4 albedoSample = (pc.albedoIdx != 0u)
        ? texture(allTextures[nonuniformEXT(pc.albedoIdx)], inUV)
        : vec4(1.0);

    vec3 albedo = albedoSample.rgb * inColor.rgb * pc.colorFactor.rgb;
    float alpha = albedoSample.a  * inColor.a   * pc.colorFactor.a;
    if (alpha < 0.5) discard;

    // -------------------------------------------------------------------------
    // METALLIC / ROUGHNESS
    // G channel = roughness, B channel = metallic (glTF spec)
    // Clamp roughness away from 0 to avoid specular fireflies.
    // -------------------------------------------------------------------------
    float roughness = pc.roughnessFactor;
    float metallic  = pc.metallicFactor;
    if (pc.metalRoughIdx != 0u) {
        vec2 mr = texture(allTextures[nonuniformEXT(pc.metalRoughIdx)], inUV).gb;
        roughness = clamp(mr.x * pc.roughnessFactor, 0.05, 1.0);
        metallic  = clamp(mr.y * pc.metallicFactor,  0.0,  1.0);
    }
    // Remap to perceptual roughness² — reduces grain at low roughness values.
    float roughnessP = roughness * roughness;

    // -------------------------------------------------------------------------
    // AMBIENT OCCLUSION
    // -------------------------------------------------------------------------
    float ao = (pc.aoIdx != 0u)
        ? texture(allTextures[nonuniformEXT(pc.aoIdx)], inUV).r
        : 1.0;

    // -------------------------------------------------------------------------
    // EMISSIVE
    // -------------------------------------------------------------------------
    vec3 emissive = vec3(0.0);
    if (pc.emissiveIdx != 0u) {
        emissive = texture(allTextures[nonuniformEXT(pc.emissiveIdx)], inUV).rgb * 2.5;
    }

    // -------------------------------------------------------------------------
    // NORMALS
    // Build TBN once, apply normal map with user-controlled strength.
    // NOTE: specularAntiAliasing (dFdx/dFdy on normal) was removed — it was
    //       the primary cause of the per-pixel grain in the previous version.
    // -------------------------------------------------------------------------
    vec3 N = normalize(inNormal);

    if (pc.normalIdx != 0u) {
        vec3 T = normalize(inTangent.xyz);
        // Re-orthogonalize against the interpolated normal (Gram-Schmidt)
        T = normalize(T - dot(T, N) * N);
        vec3 B = cross(N, T) * inTangent.w;
        mat3 TBN = mat3(T, B, N);

        vec3 nm = texture(allTextures[nonuniformEXT(pc.normalIdx)], inUV).xyz * 2.0 - 1.0;
        nm.xy   *= clamp(pc.normalStrength, 0.0, 1.0);   // strength [0,1], not unbounded
        N = normalize(TBN * normalize(nm));
    }

    // -------------------------------------------------------------------------
    // LIGHTING VECTORS
    // sunDirection is already normalised on the CPU side; normalize() here
    // just guards against any fp drift and costs almost nothing.
    // -------------------------------------------------------------------------
    vec3 V = normalize(cam.worldPosition.xyz - inWorldPos);
    vec3 L = normalize(pc.sunDirection);           // constant directional sun
    vec3 H = normalize(V + L);

    float NdotV = max(dot(N, V), EPSILON);
    float NdotL     = max(dot(N, L), 0.0);               // keep for specular (sharp highlight)
    float NdotL_hl  = dot(N, L) * 0.5 + 0.5;             // half-lambert: range [0,1], never black
    float NdotL_hl2 = NdotL_hl * NdotL_hl;  
    float NdotH = max(dot(N, H), 0.0);
    float HdotV = max(dot(H, V), 0.0);

    // -------------------------------------------------------------------------
    // PBR BRDF  (Cook-Torrance specular + Lambertian diffuse)
    // -------------------------------------------------------------------------
    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    vec3 F  = fresnelSchlick(HdotV, F0);
    float D = DistributionGGX(NdotH, roughnessP);
    float G = GeometrySmith(NdotV, NdotL, roughnessP);

    float shadowFactor = mix(0.15, 1.0, calcShadow(inWorldPos, pc.shadowBias));


    vec3 specular = (D * G * F) / (4.0 * NdotV * NdotL + EPSILON);

    vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
    vec3 diffuse = kD * albedo / PI;

    // Sun light — NdotL already clamps negative light to 0.
    const float wrap = 0.2;
    float NdotL_wrap = max((dot(N, L) + wrap) / (1.0 + wrap), 0.0);

    // Diffuse uses wrapped NdotL, specular uses hard NdotL (sharp highlight).
    vec3 directLight = (diffuse * NdotL_hl2 + specular * NdotL) * pc.sunColor * pc.sunIntensity;


    // Hemisphere ambient — ground is warm stone bounce, sky is cool blue.
    vec3 skyColor    = vec3(0.35, 0.45, 0.65);
    vec3 groundColor = vec3(0.20, 0.16, 0.10);
    float hemi       = N.y * 0.5 + 0.5;
    vec3 ambientIrr  = mix(groundColor, skyColor, hemi);
    vec3 kD_amb      = (vec3(1.0) - fresnelSchlickRoughness(NdotV, F0, roughnessP)) * (1.0 - metallic);
    vec3 ambient     = kD_amb * albedo * ambientIrr * ao * 1.5;
    vec3 ambSpec = skyColor * F0 * (1.0 - roughnessP) * ao * 0.3;

    // -------------------------------------------------------------------------
    // COMPOSITE + TONE MAP + GAMMA
    // -------------------------------------------------------------------------
    vec3 color = (directLight * shadowFactor) + ambient + ambSpec + emissive;
    color      = max(color, minLight);

    color = ACESFilm(color * 1.2);                  // exposure baked in
    color = pow(color, vec3(1.0 / 2.2));            // linear → sRGB

    outColor = vec4(color, alpha);
}
