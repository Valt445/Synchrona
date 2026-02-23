#pragma once

#include "engine.h"

bool load_texture_from_file(const char* path, Engine* e, AllocatedImage& outImage, VkSampler sampler = VK_NULL_HANDLE, VkImageLayout finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
