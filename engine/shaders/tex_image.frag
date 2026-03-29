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

const float PI = 3.14159265359;
const float MAX_REFLECTION_LOD = 4.0; 

// ============================================================================
// SHADOWS & UTILS
// ============================================================================

float calcShadow(vec3 worldPos, float bias) {
    vec4 lightSpace = cam.lightViewProj * vec4(worldPos, 1.0);
    vec3 proj = lightSpace.xyz / lightSpace.w;
    proj.xy = proj.xy * 0.5 + 0.5;
    
    if (proj.z > 1.0 || proj.x < 0.0 || proj.x > 1.0 || proj.y < 0.0 || proj.y > 1.0)
        return 1.0;
        
    float currentDepth = proj.z - bias;
    float shadow = 0.0;
    vec2 texelSize = 1.0 / vec2(2048.0); 
    
    for (int x = -1; x <= 1; x++) {
        for (int y = -1; y <= 1; y++) {
            float sampledDepth = texture(allTextures[nonuniformEXT(pc.shadowMapIndex)], 
                                         proj.xy + vec2(x, y) * texelSize).r;
            shadow += (currentDepth < sampledDepth) ? 1.0 : 0.0;
        }
    }
    return shadow / 9.0;
}

vec3 ACESFilm(vec3 x) {
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

// ============================================================================
// PBR MATH (THE PHYSICS)
// ============================================================================

float DistributionGGX(vec3 N, vec3 H, float a) {
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    float nom = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    return nom / (PI * denom * denom + 0.000001);
}

float GeometrySchlickGGX(float NdotX, float k) {
    return NdotX / (NdotX * (1.0 - k) + k + 0.000001);
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;
    return GeometrySchlickGGX(max(dot(N, V), 0.0), k) * GeometrySchlickGGX(max(dot(N, L), 0.0), k);
}

vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness) {
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// ============================================================================
// MAIN RENDER PASS
// ============================================================================
void main() {
    // 1. DATA SAMPLING & LINEARIZATION
    vec4 albedoSample = (pc.albedoIdx != 0u) ? texture(allTextures[nonuniformEXT(pc.albedoIdx)], inUV) : vec4(1.0);
    // CRITICAL: Textures are usually sRGB, PBR math must be Linear
    vec3 albedo = pow(albedoSample.rgb, vec3(2.2)) * pc.colorFactor.rgb;
    float alpha = albedoSample.a * pc.colorFactor.a;
    if (alpha < 0.1) discard;

    float roughness = pc.roughnessFactor;
    float metallic  = pc.metallicFactor;
    if (pc.metalRoughIdx != 0u) {
        vec2 mr = texture(allTextures[nonuniformEXT(pc.metalRoughIdx)], inUV).gb;
        roughness *= mr.x; // GLTF standard: Green is Roughness
        metallic  *= mr.y; // GLTF standard: Blue is Metallic
    }
    roughness = clamp(roughness, 0.04, 1.0);
    float alphaRoughness = roughness * roughness; // Perceptual roughness

    // 2. SURFACE NORMALS
    vec3 N = normalize(inNormal);
    if (pc.normalIdx != 0u) {
        vec3 T = normalize(inTangent.xyz);
        vec3 B = cross(N, T) * inTangent.w;
        vec3 nm = texture(allTextures[nonuniformEXT(pc.normalIdx)], inUV).xyz * 2.0 - 1.0;
        nm.xy *= pc.normalStrength;
        N = normalize(mat3(T, B, N) * normalize(nm));
    }

    vec3 V = normalize(cam.worldPosition.xyz - inWorldPos);
    vec3 R = reflect(-V, N);
    float NdotV = max(dot(N, V), 0.0001);

    // Dielectrics use 0.04, Metals use their Albedo
    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    // 3. DIRECT LIGHT (The Sun)
    vec3 L = normalize(pc.sunDirection);
    vec3 H = normalize(V + L);
    float NdotL = max(dot(N, L), 0.0);
    
    float D = DistributionGGX(N, H, alphaRoughness);
    float G = GeometrySmith(N, V, L, roughness);
    vec3  F = fresnelSchlick(max(dot(H, V), 0.0), F0);

    vec3 kS = F;
    vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);
    
    vec3 numerator    = D * G * F;
    float denominator = 4.0 * NdotV * NdotL + 0.0001;
    vec3 specular     = numerator / denominator;

    float shadow = calcShadow(inWorldPos, pc.shadowBias);
    vec3 directLight = (kD * albedo / PI + specular) * (pc.sunColor * pc.sunIntensity) * NdotL * shadow;

    // 4. IBL (Ambient Environment)
    vec3 F_ibl = fresnelSchlickRoughness(NdotV, F0, roughness);
    vec2 envBRDF = texture(allTextures[nonuniformEXT(pc.iblBrdfLutIndex)], vec2(NdotV, roughness)).rg;
    
    // Multi-scatter Energy Compensation (The Secret Sauce)
    vec3 energyComp = vec3(1.0) + F0 * (1.0 / envBRDF.x - 1.0);

    vec3 irradiance = texture(allCubemaps[nonuniformEXT(pc.iblIrradianceIndex)], N).rgb;
    vec3 diffuseIBL = irradiance * albedo * (1.0 - metallic);

    vec3 prefilteredColor = textureLod(allCubemaps[nonuniformEXT(pc.iblPrefilterIndex)], R, roughness * MAX_REFLECTION_LOD).rgb;
    vec3 specularIBL = prefilteredColor * (F_ibl * envBRDF.x + envBRDF.y) * energyComp;

    // Ambient Occlusion
    float ao = (pc.aoIdx != 0u) ? texture(allTextures[nonuniformEXT(pc.aoIdx)], inUV).r : 1.0;
    // Specular AO: prevents reflections in deep cracks
    float specAO = mix(1.0, ao, 0.5); 
    vec3 ambient = (diffuseIBL + specularIBL) * ao * specAO;

    // 5. EMISSIVE
    vec3 emissive = vec3(0.0);
    if(pc.emissiveIdx != 0u) {
        emissive = pow(texture(allTextures[nonuniformEXT(pc.emissiveIdx)], inUV).rgb, vec3(2.2)) * 10.0;
    }

    // 6. FINAL COMPOSITE & TONEMAPPING
    vec3 color = directLight + ambient + emissive;

    // Industry Standard Post-Process
    color = ACESFilm(color);
    color = pow(color, vec3(1.0 / 2.2)); // Back to sRGB for display

    outColor = vec4(color, alpha);
}