#version 460
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_scalar_block_layout   : require
#extension GL_EXT_ray_query             : require

layout(location = 0) in  vec3 inWorldPos;
layout(location = 1) in  vec2 inUV;
layout(location = 2) in  vec3 inNormal;
layout(location = 3) in  vec4 inColor;
layout(location = 4) in  vec4 inTangent;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D   allTextures[];
layout(set = 0, binding = 3) uniform samplerCube allCubemaps[];
layout(set = 0, binding = 4) uniform accelerationStructureEXT topLevelAS;

layout(set = 0, binding = 2) uniform CameraData {
    mat4 view;
    mat4 projection;
    mat4 viewProjection;
    vec4 worldPosition;
    mat4 lightViewProj;
} cam;

layout(scalar, push_constant) uniform constants {
    mat4  modelMatrix;
    uint  albedoIdx, normalIdx, metalRoughIdx, aoIdx, emissiveIdx;
    float metallicFactor, roughnessFactor, normalStrength;
    vec4  colorFactor;
    vec3  sunDirection;
    vec3  sunColor;
    float sunIntensity;
    uint  shadowMapIndex;
    float shadowBias;
    uint  shadowQuality;
    uint  iblIrradianceIndex, iblPrefilterIndex, iblBrdfLutIndex;
} pc;

const float PI     = 3.14159265359;
const float INV_PI = 0.31830988618;

// ── ACES fitted (Krzysztof Narkowicz) ────────────────────────────────────────
// Brighter and punchier than AgX — better for checking material quality
vec3 aces(vec3 x) {
    x *= 0.6;
    float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x*(a*x+b))/(x*(c*x+d)+e), 0.0, 1.0);
}

// ── GGX PBR ──────────────────────────────────────────────────────────────────
float D_GGX(float NdotH, float r) {
    float a2 = r*r*r*r;
    float d  = NdotH*NdotH*(a2-1.0)+1.0;
    return a2/(PI*d*d);
}
float G_Smith(float NdotV, float NdotL, float r) {
    float k = (r+1.0)*(r+1.0)*0.125;
    return (NdotV/(NdotV*(1.0-k)+k)) * (NdotL/(NdotL*(1.0-k)+k));
}
vec3 F_Schlick(float cosT, vec3 F0) {
    return F0 + (1.0-F0)*pow(clamp(1.0-cosT,0.0,1.0),5.0);
}
vec3 F_SchlickR(float cosT, vec3 F0, float r) {
    return F0 + (max(vec3(1.0-r),F0)-F0)*pow(clamp(1.0-cosT,0.0,1.0),5.0);
}

// ── PCF shadow ───────────────────────────────────────────────────────────────
const vec2 P[16] = vec2[](
    vec2(-0.94202,-0.39906),vec2( 0.94559,-0.76891),
    vec2(-0.09418,-0.92939),vec2( 0.34496, 0.29388),
    vec2(-0.91589, 0.45771),vec2(-0.81544,-0.87912),
    vec2(-0.38278, 0.27677),vec2( 0.97484, 0.75648),
    vec2( 0.44323,-0.97512),vec2( 0.53743,-0.47373),
    vec2(-0.26497,-0.41893),vec2( 0.79198, 0.19090),
    vec2(-0.24189, 0.99707),vec2(-0.81410, 0.91438),
    vec2( 0.19984, 0.78641),vec2( 0.14383,-0.14101)
);
float shadow(vec3 worldPos, vec3 N, vec3 L) {
    vec4 ls  = cam.lightViewProj * vec4(worldPos, 1.0);
    vec3 p   = ls.xyz / ls.w;
    p.xy     = p.xy * 0.5 + 0.5;
    if (any(lessThan(p.xy,vec2(0.0))) || any(greaterThan(p.xy,vec2(1.0))) || p.z>1.0)
        return 1.0;

    float bias  = clamp(pc.shadowBias * tan(acos(clamp(dot(N,L),0.0,1.0))), 0.0, pc.shadowBias*4.0);
    float depth = p.z - bias;
    vec2  ts    = 1.0/vec2(textureSize(allTextures[nonuniformEXT(pc.shadowMapIndex)],0));
    int   taps  = (pc.shadowQuality==0u)?4:(pc.shadowQuality==1u)?8:16;
    float s     = 0.0;
    for(int i=0;i<taps;++i)
        s += (depth < texture(allTextures[nonuniformEXT(pc.shadowMapIndex)], p.xy+P[i]*ts*1.5).r) ? 1.0 : 0.0;
    return s/float(taps);
}

void main() {
    // Albedo
    vec4  albedoTex = texture(allTextures[nonuniformEXT(pc.albedoIdx)], inUV);
    vec4  albedo    = albedoTex * pc.colorFactor * inColor;
    if (albedo.a < 0.01) discard;

    // Metal / Roughness
    vec4  mrTex     = texture(allTextures[nonuniformEXT(pc.metalRoughIdx)], inUV);
    float metallic  = clamp(mrTex.b * pc.metallicFactor,  0.0, 1.0);
    float roughness = clamp(mrTex.g * pc.roughnessFactor, 0.04, 1.0);

    // AO + Emissive
    float ao        = texture(allTextures[nonuniformEXT(pc.aoIdx)],      inUV).r;
    vec3  emissive  = texture(allTextures[nonuniformEXT(pc.emissiveIdx)],inUV).rgb;

    // Normal map
    vec3 N  = normalize(inNormal);
    vec3 T  = normalize(inTangent.xyz - dot(inTangent.xyz,N)*N);
    vec3 B  = cross(N,T)*sign(inTangent.w);
    vec3 nm = texture(allTextures[nonuniformEXT(pc.normalIdx)],inUV).rgb*2.0-1.0;
    nm.xy  *= pc.normalStrength;
    N = normalize(mat3(T,B,N)*nm);

    // Vectors
    vec3  V     = normalize(cam.worldPosition.xyz - inWorldPos);
    vec3  R     = reflect(-V, N);
    float NdotV = max(dot(N,V), 0.001);
    vec3  F0    = mix(vec3(0.04), albedo.rgb, metallic);

    // Sun
    vec3  L     = normalize(-pc.sunDirection);
    vec3  H     = normalize(V+L);
    float NdotL = max(dot(N,L), 0.0);
    float NdotH = max(dot(N,H), 0.0);
    float VdotH = max(dot(V,H), 0.0);
    float shad  = shadow(inWorldPos, N, L);

    vec3  F    = F_Schlick(VdotH, F0);
    vec3  kD   = (1.0-F)*(1.0-metallic);
    vec3  diff = kD * albedo.rgb * INV_PI;
    vec3  spec = (D_GGX(NdotH,roughness)*G_Smith(NdotV,NdotL,roughness)*F)
                / max(4.0*NdotV*NdotL, 0.001);

    vec3 direct = (diff+spec) * (pc.sunColor*pc.sunIntensity) * NdotL * shad;

    // IBL
    vec3 Fi    = F_SchlickR(NdotV, F0, roughness);
    vec3 kDi   = (1.0-Fi)*(1.0-metallic);
    vec3 irr   = texture(allCubemaps[nonuniformEXT(pc.iblIrradianceIndex)], N).rgb;
    vec3 pref  = textureLod(allCubemaps[nonuniformEXT(pc.iblPrefilterIndex)], R, roughness*7.0).rgb;
    vec2 brdf  = texture(allTextures[nonuniformEXT(pc.iblBrdfLutIndex)],
                         clamp(vec2(NdotV,roughness),0.0,1.0)).rg;

    vec3 ambient = (kDi*irr*albedo.rgb + pref*(Fi*brdf.x+brdf.y)) * ao;

    // Absolute minimum floor — only kicks in if IBL is completely black
    ambient = max(ambient, albedo.rgb * mix(0.02, 0.005, metallic));

    // Final
    vec3 color = direct + ambient + emissive * 2.0;
    color = aces(color);
    color = pow(max(color,0.0), vec3(1.0/2.2));

    outColor = vec4(color, albedo.a);
}
