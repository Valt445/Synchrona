#pragma once

#include <fstream>
#include <vulkan/vulkan.h>
#include <vector>

struct Utils
{
  bool load_shader_module(const char* filePath, VkDevice device, VkShaderModule* module);
};
