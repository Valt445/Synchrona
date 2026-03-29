#version 460

#extension GL_EXT_scalar_block_layout : require

layout(location = 0) out vec3 outDirection;

layout(set = 0, binding = 2) uniform CameraData {
    mat4 view;
    mat4 projection;
    mat4 viewProjection;
    vec4 worldPosition;
    mat4 lightViewProj;
} cam;

void main()
{
    // Fullscreen triangle — no vertex buffer needed
    vec2 uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    vec2 pos = uv * 2.0 - 1.0;
    gl_Position = vec4(pos, 0.999, 1.0); // depth 0.999 so geometry draws over it

    // Reconstruct view ray from NDC position
    mat4 invProj = inverse(cam.projection);
    mat4 invView = inverse(mat4(mat3(cam.view))); // rotation only

    vec4 viewRay = invProj * vec4(pos, 1.0, 1.0);
    viewRay.w = 0.0;
    outDirection = (invView * viewRay).xyz;
}