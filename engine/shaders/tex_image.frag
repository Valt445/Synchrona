#version 460

#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_scalar_block_layout : require

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
} cam;

layout(scalar, push_constant) uniform constants {
    mat4 modelMatrix;
    uint albedoIdx;
    uint normalIdx;
    uint metalRoughIdx;
    uint aoIdx;
    uint emissiveIdx;
    float metallicFactor;
    float roughnessFactor;
    float normalStrength;
    vec4 colorFactor;
    vec3 sunDirection;
    vec3 sunColor;
    float sunIntensity;
} pc;

const float PI = 3.14159265359;
const float EPSILON = 0.001;

// ============================================================================
// IMPROVED DISTRIBUTION FUNCTIONS
// ============================================================================

float DistributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    
    float nom = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;
    
    return nom / max(denom, EPSILON);
}

float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k + EPSILON);
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);
    return ggx1 * ggx2;
}

vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness) {
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// ============================================================================
// DISNEY DIFFUSE
// ============================================================================

float DisneyDiffuse(float NdotV, float NdotL, float LdotH, float roughness) {
    float energyBias = mix(0.0, 0.5, roughness);
    float energyFactor = mix(1.0, 1.0 / 1.51, roughness);
    float fd90 = energyBias + 2.0 * LdotH * LdotH * roughness;
    vec3 f0 = vec3(1.0);
    float lightScatter = fresnelSchlick(NdotL, f0).r * (1.0 + (fd90 - 1.0) * pow(1.0 - NdotL, 5.0));
    float viewScatter = fresnelSchlick(NdotV, f0).r * (1.0 + (fd90 - 1.0) * pow(1.0 - NdotV, 5.0));
    return lightScatter * viewScatter * energyFactor;
}

// ============================================================================
// SPECULAR ANTI-ALIASING
// ============================================================================

float specularAntiAliasing(vec3 normal, float roughness) {
    vec3 dndu = dFdx(normal);
    vec3 dndv = dFdy(normal);
    float variance = dot(dndu, dndu) + dot(dndv, dndv);
    float kernelRoughness = min(2.0 * variance, 1.0);
    float squareRoughness = clamp(roughness * roughness + kernelRoughness, 0.0, 1.0);
    return sqrt(squareRoughness);
}

// ============================================================================
// ACES FILMIC TONE MAPPING
// ============================================================================

vec3 ACESFilm(vec3 x) {
    float a = 2.51;
    float b = 0.03;
    float c = 2.43;
    float d = 0.59;
    float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

// ============================================================================
// MAIN SHADER
// ============================================================================

void main() {
    vec2 uv = inUV;
    
    // ========================================================================
    // MATERIAL PROPERTIES
    // ========================================================================
    
    // Albedo - textures are already in linear space, no conversion needed!
    vec3 albedo;
    if (pc.albedoIdx != 0) {
        vec4 albedoSample = texture(allTextures[nonuniformEXT(pc.albedoIdx)], uv);
        albedo = albedoSample.rgb * pc.colorFactor.rgb;  // FIXED: No pow(2.2)
    } else {
        albedo = pc.colorFactor.rgb;
    }
    
    // Metallic & Roughness
    float roughness = pc.roughnessFactor;
    float metallic = pc.metallicFactor;
    if (pc.metalRoughIdx != 0) {
        vec4 mrSample = texture(allTextures[nonuniformEXT(pc.metalRoughIdx)], uv);
        roughness = clamp(mrSample.g * pc.roughnessFactor, 0.045, 1.0);
        metallic = clamp(mrSample.b * pc.metallicFactor, 0.0, 1.0);
    }
    
    // Ambient Occlusion
    float ao = 1.0;
    if (pc.aoIdx != 0) {
        ao = texture(allTextures[nonuniformEXT(pc.aoIdx)], uv).r;
    }
    
    // Emissive - no conversion needed
    vec3 emissive = vec3(0.0);
    if (pc.emissiveIdx != 0) {
        vec3 emissiveSample = texture(allTextures[nonuniformEXT(pc.emissiveIdx)], uv).rgb;
        emissive = emissiveSample * 2.5;  // FIXED: No pow(2.2)
    }
    
    // ========================================================================
    // IMPROVED NORMAL MAPPING
    // ========================================================================
    
    vec3 N = normalize(inNormal);
    
    if (pc.normalIdx != 0) {
        vec3 tangent = normalize(inTangent.xyz);
        vec3 bitangent = cross(N, tangent) * inTangent.w;
        
        // Re-orthogonalize TBN
        tangent = normalize(tangent - N * dot(N, tangent));
        bitangent = cross(N, tangent) * inTangent.w;
        
        mat3 TBN = mat3(tangent, bitangent, N);
        
        vec3 normalMap = texture(allTextures[nonuniformEXT(pc.normalIdx)], uv).xyz * 2.0 - 1.0;
        normalMap.xy *= pc.normalStrength;
        normalMap = normalize(normalMap);
        N = normalize(TBN * normalMap);
    }
    
    // Apply specular AA
    roughness = specularAntiAliasing(N, roughness);
    
    // ========================================================================
    // LIGHTING VECTORS
    // ========================================================================
    
    vec3 V = normalize(cam.worldPosition.xyz - inWorldPos);
    vec3 L = normalize(pc.sunDirection);
    vec3 H = normalize(V + L);
    
    float NdotV = max(dot(N, V), EPSILON);
    float NdotL = max(dot(N, L), EPSILON);
    float HdotV = max(dot(H, V), 0.0);
    float LdotH = max(dot(L, H), 0.0);
    
    // ========================================================================
    // PBR BRDF
    // ========================================================================
    
    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    
    vec3 F = fresnelSchlickRoughness(HdotV, F0, roughness);
    float D = DistributionGGX(N, H, roughness);
    float G = GeometrySmith(N, V, L, roughness);
    
    vec3 numerator = D * G * F;
    float denominator = 4.0 * NdotV * NdotL + EPSILON;
    vec3 specular = numerator / denominator;
    
    vec3 kS = F;
    vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);
    
    float diffuseTerm = DisneyDiffuse(NdotV, NdotL, LdotH, roughness);
    vec3 diffuse = kD * albedo * diffuseTerm;
    
    vec3 directLight = (diffuse + specular) * NdotL * pc.sunColor * pc.sunIntensity;
    
    // ========================================================================
    // HEMISPHERE AMBIENT LIGHTING
    // ========================================================================
    
    vec3 skyColor = vec3(0.4, 0.5, 0.6);
    vec3 groundColor = vec3(0.15, 0.13, 0.11);
    float hemisphereBlend = N.y * 0.5 + 0.5;
    vec3 ambientLight = mix(groundColor, skyColor, hemisphereBlend);
    
    vec3 kS_ambient = fresnelSchlickRoughness(NdotV, F0, roughness);
    vec3 kD_ambient = (1.0 - kS_ambient) * (1.0 - metallic);
    vec3 ambient = kD_ambient * albedo * ambientLight * ao * 1.5;
    
    vec3 specularAmbient = skyColor * kS_ambient * (1.0 - roughness * roughness) * ao * 0.5;
    
    // ========================================================================
    // FINAL COLOR
    // ========================================================================
    
    vec3 color = directLight + ambient + specularAmbient + emissive;
    
    float exposure = 1.2;
    color *= exposure;
    
    color = ACESFilm(color);
    
    color = pow(color, vec3(1.0 / 2.2));
    
    outColor = vec4(color, 1.0);
}