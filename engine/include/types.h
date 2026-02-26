#pragma once

#include <cstdint>
#include <vulkan/vulkan_core.h>
#include <GLFW/glfw3.h>
#include <vector>
#include <iostream>
#include <deque>
#include <vk_mem_alloc.h>
#include <glm/gtc/matrix_transform.hpp>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <vk_video/vulkan_video_codec_av1std.h>
#include <vulkan/vulkan_core.h>
#include <VkBootstrap.h>
#include <GLFW/glfw3.h>
#include "debug_ui.h"
#include "images.h"
#include "helper.h"
#include "descriptors.h"
#include <iostream>
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_vulkan.h"
#include "imgui.h"
#include "graphics_pipeline.h"
#include <array>

struct AllocatedBuffer
{
    VkBuffer buffer;
    VmaAllocation allocation;
    VmaAllocationInfo info;
    VkDeviceAddress address;
};

// ============================================================
// CRITICAL: This struct MUST be 64 bytes and match GLSL std430 exactly.
//
// In GLSL std430, vec3 has 16-byte alignment (same as vec4), so the GPU
// inserts 4 bytes of padding after every vec3. The C++ struct must mirror
// this layout byte-for-byte, or the GPU will read every field from the
// wrong offset, causing the geometry to explode into random positions.
//
// GLSL std430 layout (what the GPU sees):
//   vec3  position → offset  0 (12 bytes) + 4 pad → next at 16
//   vec3  normal   → offset 16 (12 bytes) + 4 pad → next at 32
//   vec2  uv       → offset 32 ( 8 bytes)          → next at 40
//   vec2  _pad     → offset 40 ( 8 bytes)          → next at 48
//   vec4  color    → offset 48 (16 bytes)           → next at 64
//   struct size = 64 bytes
// ============================================================
struct Vertex {
    glm::vec3 position;  // offset  0 - 11
    float     _pad0{};   // offset 12 - 15  <- pads vec3 to 16-byte boundary
    glm::vec3 normal;    // offset 16 - 27
    float     _pad1{};   // offset 28 - 31  <- pads vec3 to 16-byte boundary
    glm::vec2 uv;        // offset 32 - 39
    glm::vec2 _pad2{};   // offset 40 - 47  <- matches GLSL "vec2 _pad"
    glm::vec4 color;     // offset 48 - 63
    // Total: 64 bytes -- matches GLSL std430 layout exactly
};

// holds the resources needed for a mesh
struct GPUMeshBuffers {

    AllocatedBuffer indexBuffer;
    AllocatedBuffer vertexBuffer;
    VkDeviceAddress vertexBufferAddress;
    uint32_t index_count;
};

// push constants for our mesh object draws
struct MeshPushConstants {
    glm::mat4 worldMatrix;
    VkDeviceAddress vertexBuffer;
    uint32_t textureIndex;
    uint32_t pad[3]; // Padding to ensure 16-byte alignment;
};