#pragma once

#include "types.h"
#include <optional>
#include <unordered_map>
#include <filesystem>
#include "cgltf.h"

struct GeoSurface
{
  uint32_t startIndex;
  uint32_t count;
  uint32_t materialIdx; // ADD THIS LINE
  uint32_t albedoTextureIndex = 0;   // bindless index (0 = white fallback)
};

struct MeshAsset
{
  std::string name;

  std::vector<GeoSurface> surfaces;
  GPUMeshBuffers meshBuffers;
};
struct Engine;



std::optional<std::vector<std::shared_ptr<MeshAsset>>> loadgltfMeshes(Engine* engine, std::filesystem::path filePath);
void upload_texture_to_bindless_safe(Engine* e, AllocatedImage img, VkSampler sampler, uint32_t index);
