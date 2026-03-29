#pragma once
#include "engine.h"  
#include "loader.h"

AllocatedImage load_hdri(Engine* e, const char* filepath);
void clean_hdri(AllocatedImage& image, Engine* e);
AllocatedImage create_cubemap_image(Engine* e, uint32_t size, VkFormat format,
    uint32_t mipLevels = 1);
void init_ibl(Engine* e);