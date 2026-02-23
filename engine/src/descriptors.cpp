#include "descriptors.h"
#include "engine.h"

void DescriptorLayoutBuilder::add_binding(uint32_t binding, VkDescriptorType type)
{
    add_binding(binding, type, 1);
}

void DescriptorLayoutBuilder::add_binding(uint32_t binding, VkDescriptorType type, uint32_t descriptorCount)
{
    VkDescriptorSetLayoutBinding newBind{};
    newBind.binding = binding;
    newBind.descriptorType = type;
    newBind.descriptorCount = descriptorCount;
    newBind.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings.push_back(newBind);
}

void DescriptorLayoutBuilder::add_bindless_array(uint32_t binding, VkDescriptorType type, uint32_t maxDescriptors)
{
    add_binding(binding, type, maxDescriptors);
}

void DescriptorLayoutBuilder::clear()
{
    bindings.clear();
}

VkDescriptorSetLayout DescriptorLayoutBuilder::build(
    VkDevice device,
    VkShaderStageFlags shaderStages,
    void* pNext,
    VkDescriptorSetLayoutCreateFlags extraFlags)
{
    for (auto& b : bindings) b.stageFlags |= shaderStages;

    // All bindings get UPDATE_AFTER_BIND + PARTIALLY_BOUND.
    // We do NOT use VARIABLE_DESCRIPTOR_COUNT — we use fixed counts declared
    // in the layout, so no VkDescriptorSetVariableDescriptorCountAllocateInfo needed.
    std::vector<VkDescriptorBindingFlags> bindingFlags(bindings.size(),
        VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT |
        VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT);

    VkDescriptorSetLayoutBindingFlagsCreateInfoEXT flagsInfo{};
    flagsInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO_EXT;
    flagsInfo.bindingCount = static_cast<uint32_t>(bindingFlags.size());
    flagsInfo.pBindingFlags = bindingFlags.data();
    // Caller passes nullptr for pNext — we own the flags chain entirely.
    flagsInfo.pNext = pNext;

    VkDescriptorSetLayoutCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    info.flags = extraFlags | VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
    info.bindingCount = static_cast<uint32_t>(bindings.size());
    info.pBindings = bindings.data();
    info.pNext = &flagsInfo;

    VkDescriptorSetLayout set = VK_NULL_HANDLE;
    VK_CHECK(vkCreateDescriptorSetLayout(device, &info, nullptr, &set));
    return set;
}

// DescriptorAllocator
void DescriptorAllocator::init_pool(VkDevice device, uint32_t maxSets, std::span<PoolSizeRatio> poolRatios)
{
    std::cout << "[INIT_POOL] Called on allocator instance at " << this << " with maxSets=" << maxSets << "\n";

    std::vector<VkDescriptorPoolSize> poolSizes;
    for (auto ratio : poolRatios) {
        poolSizes.push_back({ ratio.type, static_cast<uint32_t>(ratio.ratio * maxSets) });
    }

    VkDescriptorPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT | VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
    pool_info.maxSets = maxSets;
    pool_info.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    pool_info.pPoolSizes = poolSizes.data();

    VK_CHECK(vkCreateDescriptorPool(device, &pool_info, nullptr, &pool));
}

void DescriptorAllocator::clear_descriptors(VkDevice device)
{
    vkResetDescriptorPool(device, pool, 0);
}

void DescriptorAllocator::destroy_pool(VkDevice device)
{
    vkDestroyDescriptorPool(device, pool, nullptr);
    pool = VK_NULL_HANDLE;
}

VkDescriptorSet DescriptorAllocator::allocate(VkDevice device, VkDescriptorSetLayout layout)
{
    return allocate(device, layout, 0);
}

VkDescriptorSet DescriptorAllocator::allocate(VkDevice device, VkDescriptorSetLayout layout, uint32_t variableDescriptorCount)
{
    if (pool == VK_NULL_HANDLE) {
        std::cerr << "[FATAL] Pool not initialized!\n";
        std::abort();
    }

    VkDescriptorSetVariableDescriptorCountAllocateInfo variableInfo{};
    variableInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO;
    variableInfo.descriptorSetCount = 1;
    variableInfo.pDescriptorCounts = &variableDescriptorCount;

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = pool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &layout;
    allocInfo.pNext = (variableDescriptorCount > 0) ? &variableInfo : nullptr;

    VkDescriptorSet ds;
    VK_CHECK(vkAllocateDescriptorSets(device, &allocInfo, &ds));
    return ds;
}

// DescriptorWriter (unchanged from your version)
void DescriptorWriter::write_buffer(int binding, VkBuffer buffer, size_t size, size_t offset, VkDescriptorType type)
{
    VkDescriptorBufferInfo& info = bufferInfos.emplace_back(VkDescriptorBufferInfo{ .buffer = buffer, .offset = offset, .range = size });
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstBinding = binding;
    write.dstSet = VK_NULL_HANDLE;
    write.descriptorCount = 1;
    write.descriptorType = type;
    write.pBufferInfo = &info;
    writes.push_back(write);
}

void DescriptorWriter::write_image(int binding, VkImageView image, VkSampler sampler, VkImageLayout layout, VkDescriptorType type)
{
    VkDescriptorImageInfo& info = imageInfos.emplace_back(VkDescriptorImageInfo{ .sampler = sampler, .imageView = image, .imageLayout = layout });
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstBinding = binding;
    write.dstSet = VK_NULL_HANDLE;
    write.descriptorCount = 1;
    write.descriptorType = type;
    write.pImageInfo = &info;
    writes.push_back(write);
}

void DescriptorWriter::update_set_at_index(VkDevice device, VkDescriptorSet set, uint32_t index)
{
    for (auto& w : writes) {
        w.dstSet = set;
        w.dstArrayElement = index;
    }
    vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
}

void DescriptorWriter::update_set(VkDevice device, VkDescriptorSet set)
{
    for (auto& write : writes) write.dstSet = set;
    vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
}

void DescriptorWriter::clear()
{
    imageInfos.clear();
    bufferInfos.clear();
    writes.clear();
}

void init_descriptors(Engine* e) {
    std::cout << "Initializing high-performance bindless system (Dual-Mode)...\n";

    std::vector<DescriptorAllocator::PoolSizeRatio> sizes = {
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000.0f },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 100.0f }
    };
    e->globalDescriptorAllocator.init_pool(e->device, 10, sizes);

    DescriptorLayoutBuilder builder;
    // Keep bindings matching what the shaders declare:
    // Binding 0: COMBINED_IMAGE_SAMPLER array (textures) - fragment shader "allTextures"
    // Binding 1: STORAGE_IMAGE (draw image) - compute shader "image"
    // We use a fixed count of 4096 for binding 0 — no VARIABLE_DESCRIPTOR_COUNT needed
    // (that flag is only valid on the LAST binding, which is the storage image here).
    builder.add_bindless_array(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4096);
    builder.add_binding(1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1);

    e->bindlessLayout = builder.build(
        e->device,
        VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT,
        nullptr,
        VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT
    );

    std::cout << "[BINDLESS] Layout created successfully\n";

    // Don't pass variableDescriptorCount — we use fixed counts on both bindings.
    e->bindlessSet = e->globalDescriptorAllocator.allocate(e->device, e->bindlessLayout, 0);

    std::cout << "[BINDLESS] Set allocated successfully: " << e->bindlessSet << "\n";

    e->mainDeletionQueue.push_function([=]() {
        vkDestroyDescriptorSetLayout(e->device, e->bindlessLayout, nullptr);
        vkFreeDescriptorSets(e->device, e->globalDescriptorAllocator.pool, 1, &e->bindlessSet);
        });
}
