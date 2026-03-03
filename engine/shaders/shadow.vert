// shadow.vert
#version 460

layout(location = 0) in vec3 inPosition;

layout(push_constant) uniform ShadowPush {
    mat4 lightViewProj;  // offset  0
    mat4 modelMatrix;    // offset 64
} pc;

void main() {
    gl_Position = pc.lightViewProj * pc.modelMatrix * vec4(inPosition, 1.0);
}