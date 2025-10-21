#pragma once

#include <cstdint>
#include <vulkan/vulkan_core.h>
#include <GLFW/glfw3.h>
#include <vector>
#include <iostream>
#include <deque>
#include <bits/stdc++.h>
#include <vk_mem_alloc.h>
#include "glm/glm.hpp"
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

struct AllocatedBuffer
{
  VkBuffer buffer;
  VmaAllocation allocation;
  VmaAllocationInfo info;
  VkDeviceAddress address;
};

struct Vertex {

	glm::vec3 position;
	float uv_x;
	glm::vec3 normal;
	float uv_y;
	glm::vec4 color;
};

// holds the resources needed for a mesh
struct GPUMeshBuffers {

    AllocatedBuffer indexBuffer;
    AllocatedBuffer vertexBuffer;
    VkDeviceAddress vertexBufferAddress;
    uint32_t index_count;
};

// push constants for our mesh object draws
struct GPUDrawPushConstants {
    glm::mat4 worldMatrix;
    VkDeviceAddress vertexBuffer;
};

