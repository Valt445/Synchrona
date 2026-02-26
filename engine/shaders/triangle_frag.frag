#version 460
#extension GL_EXT_nonuniform_qualifier : require

// Ensure this is EXACTLY 'sampler2D' and an array '[]'
layout(set = 0, binding = 0) uniform sampler2D texSamplers[];

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform constants {
    layout(offset = 64) uint textureIndex; 
} pc;

void main() {
    // We use nonuniformEXT because it's a bindless array
    outColor = texture(texSamplers[nonuniformEXT(pc.textureIndex)], inUV);
}