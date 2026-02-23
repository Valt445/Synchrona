#pragma once

#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_core.h>

struct AllocatedImage
{
  VkImage image;
  VkImageView imageView;
  VmaAllocation allocation;
  VkExtent3D imageExtent;
  VkFormat imageFormat;
}; 

VkImageCreateInfo image_create_info(VkFormat format, VkImageUsageFlags usageFlags, VkExtent3D extent);
VkImageViewCreateInfo imageview_create_info(VkFormat format, VkImage image, VkImageAspectFlags aspectFlags);
VkImageSubresourceRange image_subresource_range(VkImageAspectFlags aspectMask);
void transition_image(VkCommandBuffer cmd, VkImage image, VkImageLayout currentLayout, VkImageLayout newLayout);
void copy_image_to_image(VkCommandBuffer cmd, VkImage source, VkImage destination, VkExtent3D srcSize, VkExtent2D dstSize);
