#version 460
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require // <--- Add this!

layout(set = 0, binding = 0) uniform sampler2D allTextures[];

layout(push_constant) uniform MeshPushConstants {
    mat4 worldMatrix;
    uint64_t vertexBuffer;
    uint textureIndex;
} pc;

layout(location = 0) in vec2 inUV;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inColor;

layout(location = 0) out vec4 outColor;

void main() {
    uint idx = pc.textureIndex;
    if (idx == 0) idx = 1;   // safety

    vec4 texColor = texture(allTextures[nonuniformEXT(idx)], inUV);

    // Simple smooth lighting
    vec3 N = normalize(inNormal);
    vec3 L = normalize(vec3(0.5, 1.0, -0.5));
    float lighting = max(dot(N, L), 0.3);

    outColor = vec4(texColor.rgb * lighting, texColor.a);
}
