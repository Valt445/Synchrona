#pragma once
#include <types.h>
#include <vector>
#include <memory>


struct Engine;
class MeshAsset;

void init_acceleration_structure(Engine* e, std::vector<std::shared_ptr<MeshAsset>>& meshes);
void rebuild_tlas(Engine* e);

