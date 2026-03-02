#include "engine.h"
#include "graphics_pipeline.h"
#include <cstdio>

void init_pipelines(Engine* e) {
    init_background_pipelines(e);
}

void init_background_pipelines(Engine* e)
{
    std::cout << "🛡️ 1. Starting pipeline initialization..." << std::endl;

    if (!e || e->device == VK_NULL_HANDLE) {
        std::cout << "❌ Engine or device is null\n"; std::exit(1);
    }
    std::cout << "✅ Engine and device are valid" << std::endl;
    std::cout << "🛡️ 2. Loading shader..." << std::endl;

    std::cout << "   Looking for: shaders/gradient.comp.spv" << std::endl;
    VkShaderModule computeDrawShader;
    auto loadResult1 = e->util.load_shader_module("shaders/gradient.comp.spv", e->device, &computeDrawShader);
    std::cout << "   Gradient shader load result: " << (loadResult1 ? "SUCCESS" : "FAILED") << std::endl;
    if (!loadResult1) { std::cout << "❌ GRADIENT SHADER LOAD FAILED!\n"; std::exit(1); }

    std::cout << "   Looking for: shaders/sky.comp.spv" << std::endl;
    VkShaderModule skyShader;
    auto loadResult2 = e->util.load_shader_module("shaders/sky.comp.spv", e->device, &skyShader);
    std::cout << "   Sky shader load result: " << (loadResult2 ? "SUCCESS" : "FAILED") << std::endl;
    if (!loadResult2) { std::cout << "❌ SKY SHADER LOAD FAILED!\n"; std::exit(1); }

    std::cout << "✅ Shaders loaded successfully!" << std::endl;
    std::cout << "🛡️ 3. Creating pipeline layout..." << std::endl;

    VkPushConstantRange push_constant_range = {
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        .offset = 0,
        .size = 64
    };

    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &e->bindlessLayout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &push_constant_range
    };

    if (e->bindlessLayout == VK_NULL_HANDLE) {
        std::cout << "❌ DESCRIPTOR LAYOUT IS NULL!\n"; std::exit(1);
    }

    VK_CHECK(vkCreatePipelineLayout(e->device, &pipelineLayoutInfo, nullptr, &e->gradientPipelineLayout));
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

    VK_CHECK(vkCreateComputePipelines(e->device, VK_NULL_HANDLE, 1, &computePipelineCreateInfo, nullptr, &e->gradientPipeline));

    computePipelineCreateInfo.stage.module = skyShader;
    VkPipeline skyPipeline;
    VK_CHECK(vkCreateComputePipelines(e->device, VK_NULL_HANDLE, 1, &computePipelineCreateInfo, nullptr, &skyPipeline));

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
        for (auto& effect : e->backgroundEffects)
            if (effect.pipeline != VK_NULL_HANDLE)
                vkDestroyPipeline(e->device, effect.pipeline, nullptr);
        if (e->gradientPipelineLayout != VK_NULL_HANDLE)
            vkDestroyPipelineLayout(e->device, e->gradientPipelineLayout, nullptr);
        e->backgroundEffects.clear();
        });

    vkDestroyShaderModule(e->device, computeDrawShader, nullptr);
    vkDestroyShaderModule(e->device, skyShader, nullptr);

    std::cout << "✅ Pipeline initialization complete!" << std::endl;
}

void init_mesh_pipelines(Engine* e)
{
    std::cout << "Initializing mesh pipelines...\n";

    VkShaderModule meshVertShader;
    if (!e->util.load_shader_module("shaders/colored_triangle_mesh.vert.spv", e->device, &meshVertShader)) {
        std::cerr << "Failed to load vertex shader\n"; std::exit(1);
    }

    VkShaderModule meshFragShader;
    if (!e->util.load_shader_module("shaders/tex_image.frag.spv", e->device, &meshFragShader)) {
        std::cerr << "Failed to load fragment shader\n"; std::exit(1);
    }

    // Push constant range — must match sizeof(MeshPushConstants) = 112 bytes exactly
    VkPushConstantRange pushRange{};
    pushRange.offset = 0;
    pushRange.size = sizeof(MeshPushConstants); // modelMatrix(64) + 5xuint(20) + 2xfloat(8) + pad(4) + vec4(16)
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    VkPipelineLayoutCreateInfo layoutInfo = e->util.pipeline_layout_create_info();
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &e->bindlessLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;

    VK_CHECK(vkCreatePipelineLayout(e->device, &layoutInfo, nullptr, &e->meshPipelineLayout));

    // Vertex input — matches Vertex struct: position(12) + normal(12) + uv(8) + color(16) = 48 bytes
    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = sizeof(Vertex); // 48 bytes
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::vector<VkVertexInputAttributeDescription> attributes = {
        { 0, 0, VK_FORMAT_R32G32B32_SFLOAT,    (uint32_t)offsetof(Vertex, position) },
        { 1, 0, VK_FORMAT_R32G32B32_SFLOAT,    (uint32_t)offsetof(Vertex, normal)   },
        { 2, 0, VK_FORMAT_R32G32_SFLOAT,       (uint32_t)offsetof(Vertex, uv)       },
        { 3, 0, VK_FORMAT_R32G32B32A32_SFLOAT, (uint32_t)offsetof(Vertex, color)    },
		{ 4, 0, VK_FORMAT_R32G32B32A32_SFLOAT, (uint32_t)offsetof(Vertex, tangent)  },
    };

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &binding;
    vertexInputInfo.vertexAttributeDescriptionCount = (uint32_t)attributes.size();
    vertexInputInfo.pVertexAttributeDescriptions = attributes.data();

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

    pb.vertexInputInfo = vertexInputInfo; // assign ONCE — do NOT touch it again after this
    pb.pipelineLayout = e->meshPipelineLayout;

    e->meshPipeline = build_pipeline(e->device, pb);
    if (e->meshPipeline == VK_NULL_HANDLE) {
        std::cerr << "❌ Failed to create mesh pipeline\n"; std::exit(1);
    }

    vkDestroyShaderModule(e->device, meshVertShader, nullptr);
    vkDestroyShaderModule(e->device, meshFragShader, nullptr);

    e->mainDeletionQueue.push_function([=]() {
        vkDestroyPipelineLayout(e->device, e->meshPipelineLayout, nullptr);
        vkDestroyPipeline(e->device, e->meshPipeline, nullptr);
        });

    std::cout << "✅ Mesh pipeline created with bindless layout\n";
}