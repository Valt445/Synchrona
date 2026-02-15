#pragma once


#include <cstdint>
#include <vulkan/vulkan_core.h>
#include <vector>
#include <span> 
#include <deque>
struct DescriptorLayoutBuilder
{

  std::vector<VkDescriptorSetLayoutBinding> bindings;

  void add_binding(uint32_t binding, VkDescriptorType type);
  void clear();
  VkDescriptorSetLayout build(VkDevice device, VkShaderStageFlags shaderStages, void* pNext = nullptr, VkDescriptorSetLayoutCreateFlags flags = 0);
    
};

struct DescriptorAllocator
{
  struct PoolSizeRatio
  {
    VkDescriptorType type;
    float ratio;  
  };

  VkDescriptorPool pool;

  void init_pool(VkDevice device, uint32_t maxSets, std::span<PoolSizeRatio> poolRatios);
  void clear_descriptors(VkDevice device);
  void destroy_pool(VkDevice device);

  VkDescriptorSet allocate(VkDevice device, VkDescriptorSetLayout layout);
  
};
 
struct DescriptorWriter
{
    std::deque<VkDescriptorImageInfo> imageInfos;
    std::deque<VkDescriptorBufferInfo> bufferInfos;
    std::vector<VkWriteDescriptorSet> writes;

    void write_image(int binding, VkImageView image, VkSampler sampler, VkImageLayout layout, VkDescriptorType type);
    void write_buffer(int binding, VkBuffer buffer, size_t size, size_t offset, VkDescriptorType type);

    void clear();
    void update_set(VkDevice device, VkDescriptorSet set);
};
