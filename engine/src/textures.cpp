#include "engine.h"

// Note: For complete code, see REFACTORING_DETAILED.md with line numbers
// This file contains: upload_texture_to_bindless

// Copy from engine_original.cpp:
// - upload_texture_to_bindless() - lines 1281-1411 (131 lines)

// This function handles:
// - Managing bindless texture descriptors
// - Uploading textures to GPU
// - Tracking texture indices
void upload_texture_to_bindless(Engine* e, AllocatedImage img, VkSampler sampler, uint32_t index) {
    DescriptorWriter writer;
    // Write to binding 0, at the specific array index
    writer.write_image(0, img.imageView, sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);

    // This uses the function in your descriptors.cpp to update ONLY that slot
    writer.update_set_at_index(e->device, e->bindlessSet, index);
}