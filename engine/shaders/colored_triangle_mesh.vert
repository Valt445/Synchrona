#version 460
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require  // often needed for uint64_t

struct Vertex {
    vec3 position;
    vec3 normal;
    vec2 uv;
    vec2 _pad;
    vec4 color;
};

layout(buffer_reference, std430, buffer_reference_align = 16) readonly buffer VertexBuffer {
    Vertex vertices[];
};

layout(push_constant) uniform MeshPushConstants {
    mat4 worldMatrix;
    uint64_t vertexBuffer;
    uint textureIndex;
} pc;

layout(location = 0) out vec2 outUV;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec4 outColor;

void main() {
    Vertex v = VertexBuffer(pc.vertexBuffer).vertices[gl_VertexIndex];

    gl_Position = pc.worldMatrix * vec4(v.position, 1.0);
    outUV       = v.uv;
    outNormal   = v.normal;
    outColor    = v.color;
}