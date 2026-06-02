#include "engine.h"


void upload_texture_to_bindless(Engine* e, AllocatedImage img, VkSampler sampler, uint32_t index) {
    DescriptorWriter writer;
    writer.write_image(0, img.imageView, sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    writer.update_set_at_index(e->device, e->bindlessSet, index);
}