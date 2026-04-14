#version 460
#extension GL_EXT_nonuniform_qualifier : require

layout(location = 0) in  vec3 inDirection;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 3) uniform samplerCube cubemaps[];

layout(push_constant) uniform PC {
    uint  envCubemapIndex;
    float exposure;
} pc;

// ─────────────────────────────────────────────────────────────────────────────
// AgX Tonemapper (exactly the same as your tex_image.frag)
// ─────────────────────────────────────────────────────────────────────────────
const float PI = 3.14159265359;

vec3 agxDefaultContrastApprox(vec3 x) {
    vec3 x2 = x * x;
    vec3 x4 = x2 * x2;
    return +15.5 * x4 * x2
           -40.14 * x4 * x
           +31.96 * x4
           -6.868 * x2 * x
           +0.4298 * x2
           +0.1191 * x
           -0.00232;
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

// ─────────────────────────────────────────────────────────────────────────────
void main()
{
    vec3 dir = normalize(inDirection);
    dir.y = -dir.y;   // fix for your HDR orientation

    vec3 color = texture(cubemaps[pc.envCubemapIndex], dir).rgb;

    // Apply exposure exactly like the rest of the scene
    color *= pc.exposure;

    // Same tonemapping pipeline as your helmet → perfect consistency
    color = AgX(color);
    color = AgXEotf(color);

    // Final sRGB gamma
    color = pow(max(color, vec3(0.0)), vec3(1.0 / 2.2));

    outColor = vec4(color, 1.0);
}