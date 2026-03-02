#include "engine.h"
#include <span>
#include <iostream>

// Note: For complete code, see REFACTORING_DETAILED.md with line numbers
// This file contains: uploadMesh

// Copy from engine_original.cpp:
// - uploadMesh() - lines 301-398 (98 lines)

// This function handles:
// - Loading mesh vertex/index data
// - Creating GPU buffers
// - Transferring data to GPU
GPUMeshBuffers uploadMesh(Engine* e, std::span<uint32_t> indices, std::span<Vertex> vertices) {
    GPUMeshBuffers newSurface{};

    // --- Vertex Buffer ---
    size_t vertexBufferSize = vertices.size() * sizeof(Vertex);
    if (vertexBufferSize == 0) {
        std::cerr << "❌ Vertex buffer size is 0!" << std::endl;
        return newSurface;
    }
    {
        AllocatedBuffer stagingBuffer = create_buffer(
            e->allocator,
            vertexBufferSize,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VMA_MEMORY_USAGE_CPU_ONLY
        );

        if (stagingBuffer.buffer == VK_NULL_HANDLE) {
            std::cerr << "❌ Failed to create vertex staging buffer!" << std::endl;
            return newSurface;
        }

        void* data;
        vmaMapMemory(e->allocator, stagingBuffer.allocation, &data);
        memcpy(data, vertices.data(), vertexBufferSize);
        vmaUnmapMemory(e->allocator, stagingBuffer.allocation);

        newSurface.vertexBuffer = create_buffer(
            e->allocator,
            vertexBufferSize,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            VMA_MEMORY_USAGE_GPU_ONLY
        );

        if (newSurface.vertexBuffer.buffer == VK_NULL_HANDLE) {
            std::cerr << "❌ Failed to create vertex buffer!" << std::endl;
            vmaDestroyBuffer(e->allocator, stagingBuffer.buffer, stagingBuffer.allocation);
            return newSurface;
        }

        immediate_submit([&](VkCommandBuffer cmd) {
            VkBufferCopy copyRegion{};
            copyRegion.size = vertexBufferSize;
            vkCmdCopyBuffer(cmd, stagingBuffer.buffer, newSurface.vertexBuffer.buffer, 1, &copyRegion);
            }, e);

        vmaDestroyBuffer(e->allocator, stagingBuffer.buffer, stagingBuffer.allocation);

        VkBufferDeviceAddressInfo addressInfo{};
        addressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
        addressInfo.buffer = newSurface.vertexBuffer.buffer;
        newSurface.vertexBufferAddress = vkGetBufferDeviceAddress(e->device, &addressInfo);
    }

    // --- Index Buffer ---
    size_t indexBufferSize = indices.size() * sizeof(uint32_t);
    if (indexBufferSize > 0) {
        AllocatedBuffer stagingBuffer = create_buffer(
            e->allocator,
            indexBufferSize,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VMA_MEMORY_USAGE_CPU_ONLY
        );

        if (stagingBuffer.buffer == VK_NULL_HANDLE) {
            std::cerr << "❌ Failed to create index staging buffer!" << std::endl;
            return newSurface;
        }

        void* data;
        vmaMapMemory(e->allocator, stagingBuffer.allocation, &data);
        memcpy(data, indices.data(), indexBufferSize);
        vmaUnmapMemory(e->allocator, stagingBuffer.allocation);

        newSurface.indexBuffer = create_buffer(
            e->allocator,
            indexBufferSize,
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VMA_MEMORY_USAGE_GPU_ONLY
        );

        if (newSurface.indexBuffer.buffer == VK_NULL_HANDLE) {
            std::cerr << "❌ Failed to create index buffer!" << std::endl;
            vmaDestroyBuffer(e->allocator, stagingBuffer.buffer, stagingBuffer.allocation);
            return newSurface;
        }

        immediate_submit([&](VkCommandBuffer cmd) {
            VkBufferCopy copyRegion{};
            copyRegion.size = indexBufferSize;
            vkCmdCopyBuffer(cmd, stagingBuffer.buffer, newSurface.indexBuffer.buffer, 1, &copyRegion);
            }, e);

        vmaDestroyBuffer(e->allocator, stagingBuffer.buffer, stagingBuffer.allocation);
    }

    return newSurface;
}

void calculateTangents(std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices) {
    std::vector<glm::vec3> tan1(vertices.size(), glm::vec3(0.0f));
    std::vector<glm::vec3> tan2(vertices.size(), glm::vec3(0.0f));

    for (size_t i = 0; i < indices.size(); i += 3) {
        uint32_t i1 = indices[i];
        uint32_t i2 = indices[i + 1];
        uint32_t i3 = indices[i + 2];

        glm::vec3 v1 = vertices[i1].position;
        glm::vec3 v2 = vertices[i2].position;
        glm::vec3 v3 = vertices[i3].position;

        glm::vec2 w1 = vertices[i1].uv;
        glm::vec2 w2 = vertices[i2].uv;
        glm::vec2 w3 = vertices[i3].uv;

        float x1 = v2.x - v1.x, x2 = v3.x - v1.x;
        float y1 = v2.y - v1.y, y2 = v3.y - v1.y;
        float z1 = v2.z - v1.z, z2 = v3.z - v1.z;

        float s1 = w2.x - w1.x, s2 = w3.x - w1.x;
        float t1 = w2.y - w1.y, t2 = w3.y - w1.y;

        float r = 1.0f / (s1 * t2 - s2 * t1 + 0.00001f);
        glm::vec3 sdir((t2 * x1 - t1 * x2) * r, (t2 * y1 - t1 * y2) * r, (t2 * z1 - t1 * z2) * r);
        glm::vec3 tdir((s1 * x2 - s2 * x1) * r, (s1 * y2 - s2 * y1) * r, (s1 * z2 - s2 * z1) * r);

        tan1[i1] += sdir; tan1[i2] += sdir; tan1[i3] += sdir;
        tan2[i1] += tdir; tan2[i2] += tdir; tan2[i3] += tdir;
    }

    for (size_t i = 0; i < vertices.size(); i++) {
        glm::vec3 n = glm::normalize(vertices[i].normal);
        glm::vec3 t = glm::normalize(tan1[i]);

        // Gram-Schmidt orthogonalize + fix length
        t = glm::normalize(t - n * glm::dot(n, t));

        // Handedness (the magic w component your fragment shader uses)
        float handedness = (glm::dot(glm::cross(n, t), tan2[i]) < 0.0f) ? -1.0f : 1.0f;

        vertices[i].tangent = glm::vec4(t, handedness);
    }
}