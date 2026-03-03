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
// Logging — use LOG for debug info, LOG_ERROR for fatal errors.
// In release builds (NDEBUG defined) LOG compiles to nothing.
// LOG_ERROR always prints regardless of build type.
// ============================================================
#ifdef NDEBUG
#define LOG(...)      // stripped in release
#else
#define LOG(...) std::cout << __VA_ARGS__ << "\n"
#endif
#define LOG_ERROR(...) std::cerr << "ERROR: " << __VA_ARGS__ << "\n"

// ============================================================
// AllocatedBuffer
// ============================================================
struct AllocatedBuffer {
    VkBuffer          buffer = VK_NULL_HANDLE;
    VmaAllocation     allocation = VK_NULL_HANDLE;
    VmaAllocationInfo info = {};
    VkDeviceAddress   address = 0;
};

// ============================================================
// Vertex
// Field order must match vertex attribute locations in pipelines.cpp:
//   location 0 → position  (vec3, 12 bytes)
//   location 1 → uv        (vec2,  8 bytes)  ← NOTE: uv before normal
//   location 2 → normal    (vec3, 12 bytes)
//   location 3 → color     (vec4, 16 bytes)
//   location 4 → tangent   (vec4, 16 bytes)
// Total stride: 64 bytes
// ============================================================
struct Vertex {
    glm::vec3 position; // 12 bytes — location 0
    glm::vec3 normal;   // 12 bytes — location 2 (stored here, bound by offsetof)
    glm::vec2 uv;       //  8 bytes — location 1
    glm::vec4 color;    // 16 bytes — location 3
    glm::vec4 tangent;  // 16 bytes — location 4
};                      // Total stride: 64 bytes

// ============================================================
// MeshPushConstants
// Matches fragment shader scalar push_constant block exactly.
// Total: 140 bytes — within Vulkan's 128-byte minimum guarantee? NO.
// sunColor ends at offset 136 + 4 = 140. Check your device's maxPushConstantsSize.
// ============================================================
struct MeshPushConstants {
    glm::mat4 modelMatrix;    // offset   0  (64 bytes)
    uint32_t  albedoIndex;    // offset  64
    uint32_t  normalIndex;    // offset  68
    uint32_t  metalRoughIndex;// offset  72
    uint32_t  aoIndex;        // offset  76
    uint32_t  emissiveIndex;  // offset  80
    float     metallicFactor; // offset  84
    float     roughnessFactor;// offset  88
    float     normalStrength; // offset  92
    glm::vec4 colorFactor;    // offset  96  (16 bytes)
    glm::vec3 sunDirection;   // offset 112  (12 bytes)
    glm::vec3 sunColor;       // offset 124  (12 bytes)
    float     sunIntensity; 
    
    uint32_t  shadowMapIndex;    // offset 140 — bindless slot of the shadow map
    float     shadowBias;        // offset 144 — per-material bias tweak
      
};                            // = 140 bytes


struct ShadowPushConstants {
    glm::mat4 lightViewProj;  // offset  0 (64 bytes)
    glm::mat4 modelMatrix;    // offset 64 (64 bytes)
};

// ============================================================
// GPUMeshBuffers
// ============================================================
struct GPUMeshBuffers {
    AllocatedBuffer indexBuffer;
    AllocatedBuffer vertexBuffer;
    VkDeviceAddress vertexBufferAddress = 0;
    uint32_t        indexCount = 0;
};

// ============================================================
// CameraData UBO — uploaded every frame, binding 2
// ============================================================
struct CameraData {
    glm::mat4 view;           // 64 bytes
    glm::mat4 projection;     // 64 bytes
    glm::mat4 viewProjection; // 64 bytes
    glm::vec4 worldPosition;
    glm::mat4 lightViewProj;    // 16 bytes (w unused)
};                            // = 208 bytes

// ============================================================
// GPUMaterial
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
// Node — GLTF scene graph node
// ============================================================
struct Node {
    std::string        name;
    Node* parent = nullptr;
    std::vector<Node*> children;

    glm::vec3 translation = glm::vec3(0.0f);
    glm::quat rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    glm::vec3 scale = glm::vec3(1.0f);
    glm::mat4 matrix = glm::mat4(1.0f);

    glm::mat4 worldTransform = glm::mat4(1.0f);
    uint32_t  gpuIndex = 0;
    int32_t   meshIndex = -1;

    glm::mat4 getLocalMatrix() const {
        return glm::translate(glm::mat4(1.0f), translation)
            * glm::toMat4(rotation)
            * glm::scale(glm::mat4(1.0f), scale)
            * matrix;
    }

    glm::mat4 getGlobalMatrix() const {
        glm::mat4 m = getLocalMatrix();
        const Node* p = parent;
        while (p) { m = p->getLocalMatrix() * m; p = p->parent; }
        return m;
    }
};

// ============================================================
// Animation
// ============================================================
struct AnimationSampler {
    enum class InterpolationType { LINEAR, STEP, CUBICSPLINE };
    InterpolationType      interpolation = InterpolationType::LINEAR;
    std::vector<float>     inputs;
    std::vector<glm::vec4> outputsVec4;
    std::vector<glm::vec3> outputsVec3;
};

struct AnimationChannel {
    enum class PathType { TRANSLATION, ROTATION, SCALE };
    PathType path;
    Node* node = nullptr;
    uint32_t samplerIndex = 0;
};

struct Animation {
    std::string                    name;
    std::vector<AnimationSampler>  samplers;
    std::vector<AnimationChannel>  channels;
    float start = std::numeric_limits<float>::max();
    float end = std::numeric_limits<float>::lowest();
    float currentTime = 0.0f;
};

// ============================================================
// Model
// ============================================================
struct Model {
    std::vector<Node*>       nodes;
    std::vector<Node*>       linearNodes;
    std::vector<GPUMaterial> materials;
    std::vector<Animation>   animations;

    ~Model() { for (auto* n : linearNodes) delete n; }

    Node* findNode(const std::string& name) {
        auto it = std::find_if(linearNodes.begin(), linearNodes.end(),
            [&name](const Node* n) { return n->name == name; });
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