#pragma once

#include <cstdint>
#include <vulkan/vulkan_core.h>
#include <vector>
#include <span>
#include <deque>
#include <iostream>

struct DescriptorLayoutBuilder
{
    std::vector<VkDescriptorSetLayoutBinding> bindings;

    void add_binding(uint32_t binding, VkDescriptorType type);
    void add_binding(uint32_t binding, VkDescriptorType type, uint32_t descriptorCount);
    void add_bindless_array(uint32_t binding, VkDescriptorType type, uint32_t maxDescriptors = 4096);

    void clear();

    VkDescriptorSetLayout build(
        VkDevice device,
        VkShaderStageFlags shaderStages,
        void* pNext = nullptr,
        VkDescriptorSetLayoutCreateFlags flags = 0
    );
};

struct DescriptorAllocator
{
    struct PoolSizeRatio
    {
        VkDescriptorType type;
        float ratio;
    };

    VkDescriptorPool pool = VK_NULL_HANDLE;

    DescriptorAllocator() {
        std::cout << "[CTOR] DescriptorAllocator constructed at " << this << "\n";
    }

    DescriptorAllocator(const DescriptorAllocator& other) {
        std::cout << "[COPY CTOR] Copying DescriptorAllocator from " << &other << " to " << this << "\n";
        pool = other.pool;
    }

    void init_pool(VkDevice device, uint32_t maxSets, std::span<PoolSizeRatio> poolRatios);
    void clear_descriptors(VkDevice device);
    void destroy_pool(VkDevice device);

    VkDescriptorSet allocate(VkDevice device, VkDescriptorSetLayout layout);
    VkDescriptorSet allocate(VkDevice device, VkDescriptorSetLayout layout, uint32_t variableDescriptorCount);
};

struct DescriptorWriter
{
    std::deque<VkDescriptorImageInfo> imageInfos;
    std::deque<VkDescriptorBufferInfo> bufferInfos;
    std::vector<VkWriteDescriptorSet> writes;

    void write_image(int binding, VkImageView image, VkSampler sampler, VkImageLayout layout, VkDescriptorType type);
    void write_buffer(int binding, VkBuffer buffer, size_t size, size_t offset, VkDescriptorType type);
    void update_set_at_index(VkDevice device, VkDescriptorSet set, uint32_t arrayIndex);
    void clear();
    void update_set(VkDevice device, VkDescriptorSet set);
};