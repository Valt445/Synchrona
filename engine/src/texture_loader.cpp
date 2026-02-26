#include "texture_loader.h"
#include <stb_image.h>
#include <iostream>

// Loads image file with stb_image, uploads via existing create_image helper.
// Returns true on success and fills outImage.
bool load_texture_from_file(const char* path, Engine* e, AllocatedImage& outImage, VkSampler sampler, VkImageLayout finalLayout)
{
    int texWidth = 0, texHeight = 0, texChannels = 0;
    stbi_uc* pixels = stbi_load(path, &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
    if (!pixels) {
        std::cerr << "❌ Failed to load texture: " << path << std::endl;
        return false;
    }

    VkExtent3D imageExtent = { static_cast<uint32_t>(texWidth), static_cast<uint32_t>(texHeight), 1 };

    // create_image(void* data, Engine* e, VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped)
    // This helper uploads the pixel data and transitions to SHADER_READ_ONLY_OPTIMAL.
    AllocatedImage loaded = create_image((void*)pixels, e, imageExtent, VK_FORMAT_R8G8B8A8_UNORM,
                                         VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, false);

    outImage = loaded;

    stbi_image_free(pixels);
    return true;
}
