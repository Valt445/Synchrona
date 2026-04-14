#pragma once

#include "types.h"
#include <optional>
#include <unordered_map>
#include <filesystem>
#include "cgltf.h"

// One draw call worth of geometry — all 5 PBR texture bindless indices
struct GeoSurface {
    uint32_t startIndex = 0;
    uint32_t count = 0;

    uint32_t albedoIndex = 1;
    uint32_t normalIndex = 1;
    uint32_t metallicRoughnessIndex = 1;
    uint32_t aoIndex = 1;
    uint32_t emissiveIndex = 1;

    glm::vec4 colorFactor = glm::vec4(1.0f);

    float metallicFactor = 1.0f;
    float roughnessFactor = 1.0f;
    glm::vec3 emissiveFactor = glm::vec3(0.0f);
};

// One GLTF mesh node — all surfaces share the same vertex/index buffer
struct MeshAsset {
    std::string             name;
    std::vector<GeoSurface> surfaces;
    GPUMeshBuffers          meshBuffers;
    glm::mat4               worldTransform = glm::mat4(1.0f);
    uint64_t				blasAddress{ 0 };
	VkAccelerationStructureKHR blasHandle{ VK_NULL_HANDLE };
};

struct Engine;

static constexpr uint32_t INVALID_TEXTURE = 0xFFFFFFFFu;
// Main loader entry point — matches the name engine.cpp calls
std::optional<std::vector<std::shared_ptr<MeshAsset>>>
loadgltfMeshes(Engine* e, std::filesystem::path filePath);
AllocatedImage load_image_from_gltf(Engine* e, cgltf_image* img, bool isLinear);
void upload_image_data(Engine* e, AllocatedImage& image, const void* pixels, size_t size);

void generate_mipmaps(Engine* e, VkCommandBuffer cmd, VkImage img, uint32_t mipLevels, uint32_t width, uint32_t height);
void upload_texture_to_bindless_safe(Engine* e, AllocatedImage img,
    VkSampler sampler, uint32_t index);
inline void upload_texture_to_bindless(Engine* e, AllocatedImage img,
    VkSampler sampler, uint32_t index);
