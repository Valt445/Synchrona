#include <skybox.h>

void init_skybox_pipelines(Engine* e)
{
    LOG("Building skybox pipeline...");



    VkShaderModule skyboxVertShader;
    if (!e->util.load_shader_module("shaders/skybox.vert.spv", e->device, &skyboxVertShader)) {
        LOG_ERROR("Failed to load vertex shader");
        std::exit(1);
    }

    VkShaderModule skyboxFragShader;
    if (!e->util.load_shader_module("shaders/skybox.frag.spv", e->device, &skyboxFragShader)) {
        LOG_ERROR("Failed to load fragment shader");
        std::exit(1);
    }

    VkPushConstantRange pushRange{};
    pushRange.offset = 0;
    pushRange.size = sizeof(SkyPushConstants);
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    VkPipelineLayoutCreateInfo layoutInfo = e->util.pipeline_layout_create_info();
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &e->bindlessLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;

    VK_CHECK(vkCreatePipelineLayout(e->device, &layoutInfo, nullptr, &e->skyboxPipelineLayout));


    PipelineBuilder pb;
    set_shaders(skyboxVertShader, skyboxFragShader, pb);
    set_input_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, pb);
    set_polygon_mode(VK_POLYGON_MODE_FILL, pb);
    set_cull_mode(VK_CULL_MODE_FRONT_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE, pb);
    set_multisampling(e->msaaSamples, pb);
    enable_blending_alphablend(pb);
    disable_depthtest(pb);
    set_color_attachment_format(e->drawImage.imageFormat, pb);
    set_depth_format(e->depthImage.imageFormat, pb);

    pb.pipelineLayout = e->skyboxPipelineLayout;
    pb.vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    e->skyboxPipeline = build_pipeline(e->device, pb);
    if (e->skyboxPipeline == VK_NULL_HANDLE) {
        LOG_ERROR("Failed to create skybox pipeline");
        std::exit(1);
    }



    vkDestroyShaderModule(e->device, skyboxVertShader, nullptr);
    vkDestroyShaderModule(e->device, skyboxFragShader, nullptr);

    e->mainDeletionQueue.push_function([=]() {
        vkDestroyPipelineLayout(e->device, e->skyboxPipelineLayout, nullptr);
        vkDestroyPipeline(e->device, e->skyboxPipeline, nullptr);
        });

    LOG("Skybox pipeline created");
}