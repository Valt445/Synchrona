
#define CGLTF_IMPLEMENTATION
#include "loader.h"
#include "engine.hpp"
#include "types.h"
#include <glm/gtx/quaternion.hpp>
#include <memory>
#include <stb/stb_image.h>
#include "cgltf.h" // Include the cgltf header

std::optional<std::vector<std::shared_ptr<MeshAsset>>> loadgltfMeshes(Engine* engine, std::filesystem::path filePath)
{
    std::cout << "Imma gonna load the file now " << filePath << std::endl;

    cgltf_data* data = NULL;
    cgltf_options options = {};
    cgltf_result result = cgltf_parse_file(&options, filePath.string().c_str(), &data);

    if (result != cgltf_result_success) {
        std::cout << "Failed to load the gltf: " << result << std::endl;
        return {};
    }

    result = cgltf_load_buffers(&options, data, filePath.string().c_str());
    if (result != cgltf_result_success) {
        std::cout << "Failed to load gltf buffers." << std::endl;
        cgltf_free(data);
        return {};
    }

    std::vector<std::shared_ptr<MeshAsset>> meshes;
    std::vector<uint32_t> indices;
    std::vector<Vertex> vertices;

    for (size_t mesh_index = 0; mesh_index < data->meshes_count; ++mesh_index) {
        cgltf_mesh* mesh = &data->meshes[mesh_index];
        MeshAsset newmesh;
        newmesh.name = mesh->name;

        indices.clear();
        vertices.clear();

        for (size_t primitive_index = 0; primitive_index < mesh->primitives_count; ++primitive_index) {
            cgltf_primitive* primitive = &mesh->primitives[primitive_index];
            GeoSurface newSurface;
            newSurface.startIndex = static_cast<uint32_t>(indices.size());

            // Load indices
            if (primitive->indices) {
                cgltf_accessor* indexAccessor = primitive->indices;
                newSurface.count = static_cast<uint32_t>(indexAccessor->count);
                size_t initial_vtx = vertices.size();
                indices.reserve(indices.size() + indexAccessor->count);

                for (size_t i = 0; i < indexAccessor->count; ++i) {
                    cgltf_uint index_value = cgltf_accessor_read_index(indexAccessor, i);
                    indices.push_back(static_cast<uint32_t>(index_value + initial_vtx));
                }
            } else {
                continue; // Skip primitives without indices
            }

            // Load vertex attributes
            cgltf_attribute* pos_attr = nullptr;
            cgltf_attribute* normal_attr = nullptr;
            cgltf_attribute* uv_attr = nullptr;
            cgltf_attribute* color_attr = nullptr;
            
            for (size_t attr_index = 0; attr_index < primitive->attributes_count; ++attr_index) {
                cgltf_attribute* attribute = &primitive->attributes[attr_index];
                if (attribute->type == cgltf_attribute_type_position) {
                    pos_attr = attribute;
                } else if (attribute->type == cgltf_attribute_type_normal) {
                    normal_attr = attribute;
                } else if (attribute->type == cgltf_attribute_type_texcoord) {
                    uv_attr = attribute;
                } else if (attribute->type == cgltf_attribute_type_color) {
                    color_attr = attribute;
                }
            }

            // Load vertex positions
            if (pos_attr) {
                cgltf_accessor* posAccessor = pos_attr->data;
                size_t initial_vtx_size = vertices.size();
                vertices.resize(vertices.size() + posAccessor->count);

                for (size_t i = 0; i < posAccessor->count; ++i) {
                    float pos[3];
                    cgltf_accessor_read_float(posAccessor, i, pos, 3);
                    Vertex newvtx;
                    newvtx.position = { pos[0], pos[1], pos[2] };
                    newvtx.normal = { 1, 0, 0 };
                    newvtx.color = glm::vec4 { 1.f };
                    newvtx.uv_x = 0;
                    newvtx.uv_y = 0;
                    vertices[initial_vtx_size + i] = newvtx;
                }
            }

            // Load vertex normals
            if (normal_attr) {
                cgltf_accessor* normalAccessor = normal_attr->data;
                for (size_t i = 0; i < normalAccessor->count; ++i) {
                    float normal[3];
                    cgltf_accessor_read_float(normalAccessor, i, normal, 3);
                    vertices[vertices.size() - normalAccessor->count + i].normal = { normal[0], normal[1], normal[2] };
                }
            }

            // Load UVs
            if (uv_attr) {
                cgltf_accessor* uvAccessor = uv_attr->data;
                for (size_t i = 0; i < uvAccessor->count; ++i) {
                    float uv[2];
                    cgltf_accessor_read_float(uvAccessor, i, uv, 2);
                    vertices[vertices.size() - uvAccessor->count + i].uv_x = uv[0];
                    vertices[vertices.size() - uvAccessor->count + i].uv_y = uv[1];
                }
            }

            // Load vertex colors
            if (color_attr) {
                cgltf_accessor* colorAccessor = color_attr->data;
                for (size_t i = 0; i < colorAccessor->count; ++i) {
                    float color[4];
                    cgltf_accessor_read_float(colorAccessor, i, color, 4);
                    vertices[vertices.size() - colorAccessor->count + i].color = { color[0], color[1], color[2], color[3] };
                }
            }
            newmesh.surfaces.push_back(newSurface);
        }

        // Display the vertex normals
        constexpr bool OverrideColors = true;
        if (OverrideColors) {
            for (Vertex& vtx : vertices) {
                vtx.color = glm::vec4(vtx.normal, 1.f);
            }
        }
        newmesh.meshBuffers = uploadMesh(engine, indices, vertices);
        meshes.emplace_back(std::make_shared<MeshAsset>(std::move(newmesh)));
    }

    cgltf_free(data);
    return meshes;
}
