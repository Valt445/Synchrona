#version 460
#extension GL_EXT_scalar_block_layout : require

// ── INPUTS (all at top level!) ──
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec3 inNormal;
layout(location = 3) in vec4 inColor;
layout(location = 4) in vec4 inTangent;     // ← MUST BE HERE, not inside main()

// ── OUTPUTS ──
layout(location = 0) out vec3 outWorldPos;
layout(location = 1) out vec2 outUV;
layout(location = 2) out vec3 outNormal;
layout(location = 3) out vec4 outColor;
layout(location = 4) out vec4 outTangent;

layout(scalar, push_constant) uniform constants {
    mat4 modelMatrix;
} pc;

layout(set = 0, binding = 2) uniform CameraData {
    mat4 view;
    mat4 projection;
    mat4 viewProjection;
    vec4 worldPosition;
    mat4 lightViewProj;   // ← ADD THIS
} cam;

void main() {
    vec4 worldPos = pc.modelMatrix * vec4(inPosition, 1.0);
    
    outWorldPos = worldPos.xyz;
    outUV       = inUV;
    outNormal   = normalize(mat3(pc.modelMatrix) * inNormal);
    outColor    = inColor;

    // Tangent → world space (handedness sign passed through)
    vec3 worldTangent = normalize(mat3(pc.modelMatrix) * inTangent.xyz);
    outTangent        = vec4(worldTangent, inTangent.w);

    gl_Position = cam.viewProjection * worldPos;
}