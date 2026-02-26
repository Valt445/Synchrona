#define CGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#include "loader.h"
#include "engine.h"
#include "types.h"
#include <glm/gtx/quaternion.hpp>
#include <memory>
#include "stb_image.h"


std::optional<std::vector<std::shared_ptr<MeshAsset>>> loadgltfMeshes(Engine* engine, std::filesystem::path filePath)
{
    std::cout << "Loading glTF: " << filePath << std::endl;

    cgltf_data* data = NULL;
    cgltf_options options = {};
    if (cgltf_parse_file(&options, filePath.string().c_str(), &data) != cgltf_result_success) {
        return {};
    }
    if (cgltf_load_buffers(&options, data, filePath.string().c_str()) != cgltf_result_success) {
        cgltf_free(data);
        return {};
    }

    // ==================== 1. LOAD ALL EMBEDDED TEXTURES FIRST ====================
    std::unordered_map<const cgltf_texture*, uint32_t> textureToBindless;
    engine->sceneTextures.clear();                    // start fresh
    engine->nextBindlessTextureIndex = 2;             // 0 = drawImage sampler, 1 = storage image

    for (size_t i = 0; i < data->textures_count; ++i) {
        cgltf_texture* tex = &data->textures[i];
        if (!tex->image || !tex->image->buffer_view) continue;

        // Decode image from memory (the data is inside the .glb)
        const cgltf_buffer_view* bv = tex->image->buffer_view;
        const unsigned char* rawData = (const unsigned char*)bv->buffer->data + bv->offset;

        int w, h, channels;
        stbi_uc* pixels = stbi_load_from_memory(rawData, (int)bv->size, &w, &h, &channels, 4);
        if (!pixels) {
            std::cerr << "❌ Failed to decode texture " << i << "\n";
            continue;
        }

        // Create Vulkan image (mipmapped = true looks much better)
        AllocatedImage img = create_image(pixels, engine,
            VkExtent3D{ (uint32_t)w, (uint32_t)h, 1 },
            VK_FORMAT_R8G8B8A8_UNORM,
            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            true); // mipmapped

        stbi_image_free(pixels);

        // Upload to our bindless descriptor set (starting at index 1)
        uint32_t bindlessIdx = engine->nextBindlessTextureIndex++;
        std::cout << "About to upload safe to index " << bindlessIdx << std::endl;
        upload_texture_to_bindless_safe(engine, img, engine->defaultSamplerLinear, bindlessIdx);

        textureToBindless[tex] = bindlessIdx;
        engine->sceneTextures.push_back(img);   // keep it alive forever
    }

    // fallback: if no textures, use white (index 0)
    if (textureToBindless.empty()) {
        textureToBindless[nullptr] = 1;
    }

    // ==================== 2. LOAD MESHES + ASSIGN TEXTURE INDEX ====================
    std::vector<std::shared_ptr<MeshAsset>> meshes;

    for (size_t mesh_idx = 0; mesh_idx < data->meshes_count; ++mesh_idx) {
        cgltf_mesh* mesh = &data->meshes[mesh_idx];
        MeshAsset newmesh;
        newmesh.name = mesh->name ? mesh->name : "unnamed";

        std::vector<uint32_t> indices;
        std::vector<Vertex> vertices;

        for (size_t prim_idx = 0; prim_idx < mesh->primitives_count; ++prim_idx) {
            cgltf_primitive* prim = &mesh->primitives[prim_idx];

            GeoSurface surf{};
            surf.startIndex = static_cast<uint32_t>(indices.size());

            // === THIS IS THE KEY LINE ===
            // Look at the material → baseColorTexture → which texture index we stored
            if (prim->material && prim->material->pbr_metallic_roughness.base_color_texture.texture) {
                auto* gltfTex = prim->material->pbr_metallic_roughness.base_color_texture.texture;
                surf.albedoTextureIndex = textureToBindless.count(gltfTex) ? textureToBindless[gltfTex] : 0;
            }
            else {
                surf.albedoTextureIndex = 1; // white fallback
            }

            // === Rest of your original index/vertex loading (unchanged) ===
            if (prim->indices) {
                cgltf_accessor* acc = prim->indices;
                surf.count = static_cast<uint32_t>(acc->count);
                uint32_t vtxOffset = static_cast<uint32_t>(vertices.size());
                for (size_t i = 0; i < acc->count; ++i) {
                    indices.push_back(static_cast<uint32_t>(cgltf_accessor_read_index(acc, i)) + vtxOffset);
                }
            }

            size_t start_vtx = vertices.size();
            size_t vcount = 0;
            for (size_t a = 0; a < prim->attributes_count; ++a) {
                if (prim->attributes[a].type == cgltf_attribute_type_position) {
                    vcount = prim->attributes[a].data->count;
                    break;
                }
            }
            if (vcount == 0) continue; // skip degenerate primitive
            vertices.resize(start_vtx + vcount);

            // FIX: Pre-initialize every vertex color to white (1,1,1,1).
            // Most meshes (including the teapot) have no COLOR_0 attribute, so
            // vertices stay zero-initialized from resize(). When the shader does
            // texColor * vertexColor, zero color makes the entire mesh black.
            for (size_t i = 0; i < vcount; ++i) {
                vertices[start_vtx + i].color = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
            }

            for (size_t a = 0; a < prim->attributes_count; ++a) {
                cgltf_attribute* attr = &prim->attributes[a];
                cgltf_accessor* acc = attr->data;
                for (size_t i = 0; i < acc->count; ++i) {
                    Vertex& v = vertices[start_vtx + i];
                    if (attr->type == cgltf_attribute_type_position)
                        cgltf_accessor_read_float(acc, i, &v.position.x, 3);
                    else if (attr->type == cgltf_attribute_type_normal)
                        cgltf_accessor_read_float(acc, i, &v.normal.x, 3);
                    else if (attr->type == cgltf_attribute_type_texcoord)
                        cgltf_accessor_read_float(acc, i, &v.uv.x, 2);
                    else if (attr->type == cgltf_attribute_type_color)
                        cgltf_accessor_read_float(acc, i, &v.color.x, 4);
                }
            }

            newmesh.surfaces.push_back(surf);
        }

        newmesh.meshBuffers = uploadMesh(engine, indices, vertices);
        meshes.emplace_back(std::make_shared<MeshAsset>(std::move(newmesh)));
    }

    cgltf_free(data);
    std::cout << "✅ Loaded " << meshes.size() << " meshes with "
        << engine->sceneTextures.size() << " embedded textures!\n";
    return meshes;
}


void upload_texture_to_bindless_safe(Engine* e, AllocatedImage img, VkSampler sampler, uint32_t index)
{
    std::cout << "Uploading texture to bindless index " << index << std::endl;

    VkDescriptorImageInfo imageInfo{};
    imageInfo.sampler = sampler;
    imageInfo.imageView = img.imageView;
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = e->bindlessSet;
    write.dstBinding = 0;                    // binding 0 = COMBINED_IMAGE_SAMPLER array
    write.dstArrayElement = index;                // ← THIS IS THE IMPORTANT LINE
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &imageInfo;

    vkUpdateDescriptorSets(e->device, 1, &write, 0, nullptr);
}