#pragma once
#define GLM_ENABLE_EXPERIMENTAL

#include <cstdint>
#include <vulkan/vulkan_core.h>
#include <GLFW/glfw3.h>
#include <vector>
#include <iostream>
#include <deque>
#include <vk_mem_alloc.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/glm.hpp>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <string>
#include <limits>
#include <cassert>
#include <VkBootstrap.h>
#include "debug_ui.h"
#include "images.h"
#include "helper.h"
#include "descriptors.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_vulkan.h"
#include "imgui.h"
#include "graphics_pipeline.h"
#include <array>
#include <stb_image.h>
// ============================================================
// AllocatedBuffer
// ============================================================
struct AllocatedBuffer
{
    VkBuffer        buffer = VK_NULL_HANDLE;
    VmaAllocation   allocation = VK_NULL_HANDLE;
    VmaAllocationInfo info = {};
    VkDeviceAddress address = 0;
};

// ============================================================
// Vertex
// Layout matches GL_EXT_scalar_block_layout in the vertex shader.
// With scalar layout, vec3 is packed (12 bytes, no padding).
// Total: 12 + 12 + 8 + 16 = 48 bytes.
// DO NOT add getBindingDescription / getAttributeDescriptions —
// this engine uses buffer device address vertex pulling.
// The pipeline has zero vertex input state.
// ============================================================

// types.h

// 1. Updated Vertex with explicit padding for 16-byte alignment

// types.h
struct Vertex {
    glm::vec3 position; // 12 bytes
    glm::vec3 normal;   // 12 bytes
    glm::vec2 uv;       // 8 bytes
    glm::vec4 color;
    glm::vec4 tangent;    // 16 bytes
}; // Total Stride: 48 bytes

// ============================================================
// Push constants — must stay <= 128 bytes (Vulkan minimum guarantee)
// mat4 = 64, uint64 = 8, 5x uint32 = 20, pad 4 = 4 -> 100 bytes total, aligned.
// ============================================================

// 2. Updated Push Constants (Total: 112 bytes)
// types.h
// types.h
// Matches shader push_constant block EXACTLY — scalar layout, 112 bytes total.
// DO NOT add viewProj here — camera data comes from the CameraData UBO (binding 2).
struct MeshPushConstants {
    glm::mat4 modelMatrix;      
    uint32_t  albedoIndex;      
    uint32_t  normalIndex;     
    uint32_t  metalRoughIndex;  
    uint32_t  aoIndex;          
    uint32_t  emissiveIndex;   
    float     metallicFactor;  
    float     roughnessFactor;  
    float     pad;              
    glm::vec4 colorFactor;
    glm::vec3 sunDirection;
	glm::vec3 sunColor;
	float    sunIntensity;
	float     normalStrength;
};                              // = 112 bytes — matches pushRange.size in pipelines.cpp
// ============================================================
// GPU mesh buffers — uploaded once, never changed
// ============================================================
struct GPUMeshBuffers {
    AllocatedBuffer indexBuffer;
    AllocatedBuffer vertexBuffer;
    VkDeviceAddress vertexBufferAddress = 0;
    uint32_t        indexCount = 0;
};


// ============================================================
// Camera UBO — uploaded every frame via update_uniform_buffers()
// Matches layout(binding = 2) uniform CameraData in shaders.
// ============================================================
struct CameraData {
    glm::mat4 view;            // 64 bytes
    glm::mat4 projection;      // 64 bytes
    glm::mat4 viewProjection;  // 64 bytes
    glm::vec4 worldPosition;   // 16 bytes — w unused, required for alignment
};                             // = 208 bytes

// ============================================================
// PBR material — one per GLTF material primitive
// Indices refer to bindless texture slots (uint32_t max = no texture).
// ============================================================
struct GPUMaterial {
    uint32_t  albedoIndex = UINT32_MAX;
    uint32_t  normalIndex = UINT32_MAX;
    uint32_t  metallicRoughnessIndex = UINT32_MAX;
    uint32_t  aoIndex = UINT32_MAX;
    uint32_t  emissiveIndex = UINT32_MAX;
    float     metallicFactor = 1.0f;
    float     roughnessFactor = 1.0f;
    float     pad = 0.0f;
    glm::vec4 baseColorFactor = glm::vec4(1.0f);
    glm::vec4 emissiveFactor = glm::vec4(0.0f);
};

// ============================================================
// GeoSurface — one draw call worth of geometry inside a MeshAsset
// ============================================================

// ============================================================
// Node — GLTF scene graph node
// ============================================================
struct Node {
    std::string         name;
    Node* parent = nullptr;
    std::vector<Node*>  children;

    // Local transform components (from GLTF TRS or matrix)
    glm::vec3           translation = glm::vec3(0.0f);
    glm::quat           rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    glm::vec3           scale = glm::vec3(1.0f);
    glm::mat4           matrix = glm::mat4(1.0f); // base matrix from GLTF node.matrix

    // Cached world transform — updated by update_uniform_buffers() every frame
    glm::mat4           worldTransform = glm::mat4(1.0f);

    // Index into the nodeBuffer on the GPU — assigned during init
    uint32_t            gpuIndex = 0;

    // Mesh index this node owns (-1 = no mesh, just a transform node)
    int32_t             meshIndex = -1;

    glm::mat4 getLocalMatrix() const {
        return glm::translate(glm::mat4(1.0f), translation)
            * glm::toMat4(rotation)
            * glm::scale(glm::mat4(1.0f), scale)
            * matrix;
    }

    // Walks parent chain — only call during init or when hierarchy changes.
    // For per-frame updates use the cached worldTransform instead.
    glm::mat4 getGlobalMatrix() const {
        glm::mat4 m = getLocalMatrix();
        const Node* p = parent;
        while (p) {
            m = p->getLocalMatrix() * m;
            p = p->parent;
        }
        return m;
    }
};

// ============================================================
// Animation
// ============================================================
struct AnimationSampler {
    enum class InterpolationType { LINEAR, STEP, CUBICSPLINE };
    InterpolationType       interpolation = InterpolationType::LINEAR;
    std::vector<float>      inputs;        // timestamps
    std::vector<glm::vec4>  outputsVec4;   // rotation keyframes
    std::vector<glm::vec3>  outputsVec3;   // translation / scale keyframes
};

struct AnimationChannel {
    enum class PathType { TRANSLATION, ROTATION, SCALE };
    PathType   path;
    Node* node = nullptr;
    uint32_t   samplerIndex = 0;
};

struct Animation {
    std::string                     name;
    std::vector<AnimationSampler>   samplers;
    std::vector<AnimationChannel>   channels;
    float   start = std::numeric_limits<float>::max();
    float   end = std::numeric_limits<float>::lowest();
    float   currentTime = 0.0f;
};

// ============================================================
// Model — top-level GLTF scene container
// ============================================================
struct Model {
    std::vector<Node*>      nodes;       // root nodes only
    std::vector<Node*>      linearNodes; // all nodes flat, for iteration
    std::vector<GPUMaterial> materials;
    std::vector<Animation>  animations;

    ~Model() {
        for (auto* node : linearNodes) delete node;
    }

    // Find a node by name. Returns nullptr if not found.
    Node* findNode(const std::string& name) {
        auto it = std::find_if(linearNodes.begin(), linearNodes.end(),
            [&name](const Node* node) {
                return node->name == name;
            });
        return (it != linearNodes.end()) ? *it : nullptr;
    }

    void updateAnimation(uint32_t index, float deltaTime) {
        assert(index < animations.size() && "Animation index out of range");
        Animation& anim = animations[index];
        anim.currentTime += deltaTime;
        if (anim.currentTime > anim.end) anim.currentTime = anim.start;

        for (auto& channel : anim.channels) {
            AnimationSampler& sampler = anim.samplers[channel.samplerIndex];

            auto keyIt = std::lower_bound(sampler.inputs.begin(), sampler.inputs.end(), anim.currentTime);
            if (keyIt == sampler.inputs.end() || keyIt == sampler.inputs.begin()) continue;

            size_t i = std::distance(sampler.inputs.begin(), keyIt) - 1;
            float  t = (anim.currentTime - sampler.inputs[i])
                / (sampler.inputs[i + 1] - sampler.inputs[i]);

            switch (channel.path) {
            case AnimationChannel::PathType::TRANSLATION:
                channel.node->translation = glm::mix(sampler.outputsVec3[i], sampler.outputsVec3[i + 1], t);
                break;
            case AnimationChannel::PathType::ROTATION: {
                glm::quat q0(sampler.outputsVec4[i].w, sampler.outputsVec4[i].x, sampler.outputsVec4[i].y, sampler.outputsVec4[i].z);
                glm::quat q1(sampler.outputsVec4[i + 1].w, sampler.outputsVec4[i + 1].x, sampler.outputsVec4[i + 1].y, sampler.outputsVec4[i + 1].z);
                channel.node->rotation = glm::normalize(glm::slerp(q0, q1, t));
                break;
            }
            case AnimationChannel::PathType::SCALE:
                channel.node->scale = glm::mix(sampler.outputsVec3[i], sampler.outputsVec3[i + 1], t);
                break;
            }
        }
    }
};