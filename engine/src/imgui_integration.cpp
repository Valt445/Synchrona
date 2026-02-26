#include "engine.h"
#include "imgui.h"
#include <GLFW/glfw3.h>
#include <cstdint>

// Note: For complete code, see REFACTORING_DETAILED.md with line numbers
// This file contains: init_imgui

// Copy from engine_original.cpp:
// - init_imgui() - lines 190-246 (57 lines)

// This function handles:
// - ImGui context creation
// - Vulkan backend initialization
// - Descriptor pool setup for ImGui
void init_imgui(Engine* e)
{
    VkDescriptorPoolSize pool_sizes[] = {
        { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
    };

    VkDescriptorPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
    pool_info.maxSets = 1000;
    pool_info.poolSizeCount = (uint32_t)std::size(pool_sizes);
    pool_info.pPoolSizes = pool_sizes;

    VK_CHECK(vkCreateDescriptorPool(e->device, &pool_info, nullptr, &e->imguiDescriptorPool));

    ImGui::CreateContext();
    ImGui_ImplGlfw_InitForVulkan(e->window, true);

    VkPipelineRenderingCreateInfo pipelineRenderingInfo{};
    pipelineRenderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    pipelineRenderingInfo.colorAttachmentCount = 1;
    pipelineRenderingInfo.pColorAttachmentFormats = &e->swapchainImageFormat;

    ImGui_ImplVulkan_InitInfo init_info = {};
    init_info.Instance = e->instance;
    init_info.PhysicalDevice = e->physicalDevice;
    init_info.Device = e->device;
    init_info.Queue = e->graphicsQueue;
    init_info.DescriptorPool = e->imguiDescriptorPool;

    uint32_t scImageCount = static_cast<uint32_t>(e->swapchainImages.size());
    if (scImageCount == 0) scImageCount = 2;
    init_info.MinImageCount = scImageCount;
    init_info.ImageCount = scImageCount;
    init_info.UseDynamicRendering = VK_TRUE;
    init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    init_info.PipelineRenderingCreateInfo = pipelineRenderingInfo;

    ImGui_ImplVulkan_Init(&init_info);

    debug_ui_init(e);
    e->mainDeletionQueue.push_function([=]() {
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        vkDestroyDescriptorPool(e->device, e->imguiDescriptorPool, nullptr);
        });
}