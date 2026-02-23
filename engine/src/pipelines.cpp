#include "engine.h"
#include "graphics_pipeline.h"
#include <cstdio>

// Note: For complete code, see REFACTORING_DETAILED.md with line numbers
// This file contains: init_pipelines, init_background_pipelines, init_mesh_pipelines

// Copy from engine_original.cpp the functions listed in REFACTORING_DETAILED.md:
// - init_pipelines() - lines 186-188
// - init_background_pipelines() - lines 940-1081 (LARGE, 142 lines)
// - init_mesh_pipelines() - lines 1083-1148

// These functions handle:
// - Creating graphics pipelines
// - Setting up compute pipelines for effects
// - Pipeline layout setup and management
void init_pipelines(Engine* e) {
    init_background_pipelines(e);
}
void init_background_pipelines(Engine* e) {
    std::cout << "🛡️ 1. Starting pipeline initialization..." << std::endl;

    if (e == nullptr) {
        std::cout << "❌ ENGINE IS NULL!" << std::endl;
        std::exit(1);
    }
    if (e->device == VK_NULL_HANDLE) {
        std::cout << "❌ DEVICE IS NULL!" << std::endl;
        std::exit(1);
    }
    std::cout << "✅ Engine and device are valid" << std::endl;

    std::cout << "🛡️ 2. Loading shader..." << std::endl;

    // ADD DEBUG OUTPUT FOR SHADER PATHS
    std::cout << "   Looking for: shaders/gradient.comp.spv" << std::endl;

    VkShaderModule computeDrawShader;
    auto loadResult1 = e->util.load_shader_module("shaders/gradient.comp.spv", e->device, &computeDrawShader);
    std::cout << "   Gradient shader load result: " << (loadResult1 ? "SUCCESS" : "FAILED") << std::endl;

    if (!loadResult1) {
        std::cout << "❌ GRADIENT SHADER LOAD FAILED!" << std::endl;
        std::exit(1);
    }

    std::cout << "   Looking for: shaders/sky.comp.spv" << std::endl;
    VkShaderModule skyShader;
    auto loadResult2 = e->util.load_shader_module("shaders/sky.comp.spv", e->device, &skyShader);
    std::cout << "   Sky shader load result: " << (loadResult2 ? "SUCCESS" : "FAILED") << std::endl;

    if (!loadResult2) {
        std::cout << "❌ SKY SHADER LOAD FAILED!" << std::endl;
        std::exit(1);
    }

    std::cout << "✅ Shaders loaded successfully!" << std::endl;


    constexpr uint32_t SHADER_PUSH_CONSTANT_SIZE = 64;
    std::cout << "🛡️ 3. Creating pipeline layout..." << std::endl;
    VkPushConstantRange push_constant_range = {
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        .offset = 0,
        .size = SHADER_PUSH_CONSTANT_SIZE
    };

    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &e->bindlessLayout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &push_constant_range
    };

    if (e->bindlessLayout == VK_NULL_HANDLE) {
        std::cout << "❌ DESCRIPTOR LAYOUT IS NULL!" << std::endl;
        std::exit(1);
    }

    VkResult layoutResult = vkCreatePipelineLayout(e->device, &pipelineLayoutInfo, nullptr, &e->gradientPipelineLayout);
    if (layoutResult != VK_SUCCESS) {
        std::cout << "❌ PIPELINE LAYOUT CREATION FAILED: " << layoutResult << std::endl;
        std::exit(1);
    }
    std::cout << "✅ Pipeline layout created!" << std::endl;

    std::cout << "🛡️ 4. Creating compute pipeline..." << std::endl;
    VkPipelineShaderStageCreateInfo stageinfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_COMPUTE_BIT,
        .module = computeDrawShader,
        .pName = "main"
    };

    VkComputePipelineCreateInfo computePipelineCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage = stageinfo,
        .layout = e->gradientPipelineLayout
    };

    VkResult pipelineResult = vkCreateComputePipelines(e->device, VK_NULL_HANDLE, 1, &computePipelineCreateInfo, nullptr, &e->gradientPipeline);
    if (pipelineResult != VK_SUCCESS) {
        std::cout << "❌ GRADIENT PIPELINE CREATION FAILED: " << pipelineResult << std::endl;
        std::exit(1);
    }

    computePipelineCreateInfo.stage.module = skyShader;
    VkPipeline skyPipeline;
    VkResult skyPipelineResult = vkCreateComputePipelines(e->device, VK_NULL_HANDLE, 1, &computePipelineCreateInfo, nullptr, &skyPipeline);
    if (skyPipelineResult != VK_SUCCESS) {
        std::cout << "❌ SKY PIPELINE CREATION FAILED: " << skyPipelineResult << std::endl;
        std::exit(1);
    }

    ComputeEffect gradient;
    gradient.pipeline = e->gradientPipeline;
    gradient.layout = e->gradientPipelineLayout;
    gradient.name = "gradient";
    gradient.effectData = {};
    gradient.effectData.data1 = glm::vec4(1, 0, 0, 1);
    gradient.effectData.data2 = glm::vec4(0, 0, 1, 1);

    ComputeEffect sky;
    sky.pipeline = skyPipeline;
    sky.layout = e->gradientPipelineLayout;
    sky.name = "sky";
    sky.effectData = {};
    sky.effectData.data1 = glm::vec4(0.1, 0.2, 0.4, 0.97);

    e->backgroundEffects.push_back(gradient);
    e->backgroundEffects.push_back(sky);

    std::cout << "✅ Compute pipelines created!" << std::endl;

    e->mainDeletionQueue.push_function([=]() {
        std::cout << "Cleaning up pipelines..." << std::endl;

        for (auto& effect : e->backgroundEffects) {
            if (effect.pipeline != VK_NULL_HANDLE) {
                vkDestroyPipeline(e->device, effect.pipeline, nullptr);
                effect.pipeline = VK_NULL_HANDLE;
            }
        }

        if (e->gradientPipelineLayout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(e->device, e->gradientPipelineLayout, nullptr);
            e->gradientPipelineLayout = VK_NULL_HANDLE;
        }

        e->backgroundEffects.clear();

        std::cout << "Pipeline cleanup complete!" << std::endl;
        });

    vkDestroyShaderModule(e->device, computeDrawShader, nullptr);
    vkDestroyShaderModule(e->device, skyShader, nullptr);

    std::cout << "✅ Pipeline initialization complete!" << std::endl;
}


void init_mesh_pipelines(Engine* e)
{
    std::cout << "Initializing mesh pipelines...\n";

    // 1. Load shaders
    VkShaderModule meshVertShader;
    if (!e->util.load_shader_module("shaders/colored_triangle_mesh.vert.spv", e->device, &meshVertShader)) {
        std::cerr << "Failed to load vertex shader\n"; std::exit(1);
    }

    VkShaderModule meshFragShader;
    if (!e->util.load_shader_module("shaders/tex_image.frag.spv", e->device, &meshFragShader)) {
        std::cerr << "Failed to load fragment shader\n"; std::exit(1);
    }

    // 2. Push constants - MUST match MeshPushConstants exactly (88 bytes)
    VkPushConstantRange pushRange{};
    pushRange.offset = 0;
    pushRange.size = sizeof(MeshPushConstants);   // 88 bytes
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    // 3. Pipeline layout with bindless set
    VkPipelineLayoutCreateInfo layoutInfo = e->util.pipeline_layout_create_info();
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &e->bindlessLayout;   // ← important
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;

    VK_CHECK(vkCreatePipelineLayout(e->device, &layoutInfo, nullptr, &e->meshPipelineLayout));

    // 4. Build pipeline
    PipelineBuilder pb;
    set_shaders(meshVertShader, meshFragShader, pb);
    set_input_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, pb);
    set_polygon_mode(VK_POLYGON_MODE_FILL, pb);
    set_cull_mode(VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE, pb);
    set_multisampling_none(pb);
    enable_blending_alphablend(pb);
    enable_depthtest(pb, VK_COMPARE_OP_LESS_OR_EQUAL);

    set_color_attachment_format(e->drawImage.imageFormat, pb);
    set_depth_format(e->depthImage.imageFormat, pb);

    pb.pipelineLayout = e->meshPipelineLayout;

    // Empty vertex input (we use buffer reference)
    pb.vertexInputInfo = {};
    pb.vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    e->meshPipeline = build_pipeline(e->device, pb);
    if (e->meshPipeline == VK_NULL_HANDLE) {
        std::cerr << "❌ Failed to create mesh pipeline\n"; std::exit(1);
    }

    // Cleanup shaders
    vkDestroyShaderModule(e->device, meshVertShader, nullptr);
    vkDestroyShaderModule(e->device, meshFragShader, nullptr);

    e->mainDeletionQueue.push_function([=]() {
        vkDestroyPipelineLayout(e->device, e->meshPipelineLayout, nullptr);
        vkDestroyPipeline(e->device, e->meshPipeline, nullptr);
        });

    std::cout << "✅ Mesh pipeline created with bindless layout\n";
}
