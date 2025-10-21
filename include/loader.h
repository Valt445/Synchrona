#pragma once

#include "types.h"
#include <optional>
#include <unordered_map>
#include <filesystem>

struct GeoSurface
{
  uint32_t startIndex;
  uint32_t count;
  
};

struct MeshAsset
{
  std::string name;

  std::vector<GeoSurface> surfaces;
  GPUMeshBuffers meshBuffers;
};
struct Engine;



std::optional<std::vector<std::shared_ptr<MeshAsset>>> loadgltfMeshes(Engine* engine, std::filesystem::path filePath);
