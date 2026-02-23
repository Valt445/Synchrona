#pragma once

#include <fstream>
#include <vulkan/vulkan.h>
#include <vector>
#include <vulkan/vulkan_core.h>

struct Utils
{
  VkRenderingInfo rendering_info(VkExtent2D renderextent, VkRenderingAttachmentInfo* colorAttachment, VkRenderingAttachmentInfo* depthAttachment);
  bool load_shader_module(const char* filePath, VkDevice device, VkShaderModule* module);
  VkPipelineShaderStageCreateInfo pipeline_shader_stage_create_info(VkShaderStageFlagBits stage, VkShaderModule shaderModule, const char* entry);
  VkPipelineLayoutCreateInfo pipeline_layout_create_info();
};
