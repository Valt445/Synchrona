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
