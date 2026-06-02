#include "engine.h"
#include "graphics_pipeline.h"

void init_pipelines(Engine* e)
{
    init_background_pipelines(e);
}

void init_background_pipelines(Engine* e)
{
    VkShaderModule computeDrawShader;
    if (!e->util.load_shader_module("shaders/gradient.comp.spv", e->device, &computeDrawShader)) {
        LOG_ERROR("Failed to load gradient.comp.spv");
        std::exit(1);
    }


    VkShaderModule skyAtmoShader;
    if (!e->util.load_shader_module("shaders/sky.comp.spv", e->device, &skyAtmoShader)) {
        LOG_ERROR("Failed to load sky_atmo.comp.spv");
        std::exit(1);
    }

    VkPushConstantRange push_constant_range{
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        .offset = 0,
        .size = sizeof(ScenePushConstants)
    };

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &e->bindlessLayout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &push_constant_range
    };

    VK_CHECK(vkCreatePipelineLayout(e->device, &pipelineLayoutInfo, nullptr,
        &e->gradientPipelineLayout));

    auto makeComputePipeline = [&](VkShaderModule mod) -> VkPipeline {
        VkPipelineShaderStageCreateInfo stage{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_COMPUTE_BIT,
            .module = mod,
            .pName = "main"
        };
        VkComputePipelineCreateInfo info{
            .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
            .stage = stage,
            .layout = e->gradientPipelineLayout
        };
        VkPipeline pipeline;
        VK_CHECK(vkCreateComputePipelines(e->device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline));
        return pipeline;
        };

    e->gradientPipeline = makeComputePipeline(computeDrawShader);
  
    VkPipeline skyAtmoPipeline = makeComputePipeline(skyAtmoShader);

    ComputeEffect gradient;
    gradient.pipeline = e->gradientPipeline;
    gradient.layout = e->gradientPipelineLayout;
    gradient.name = "gradient";
    gradient.effectData = {};
    gradient.effectData.data1 = glm::vec4(1, 0, 0, 1);
    gradient.effectData.data2 = glm::vec4(0, 0, 1, 1);


    ComputeEffect skyAtmo;
    skyAtmo.pipeline = skyAtmoPipeline;
    skyAtmo.layout = e->gradientPipelineLayout;
    skyAtmo.name = "sky_atmo";
    skyAtmo.effectData = {};
    skyAtmo.effectData.data1 = glm::vec4(
        glm::normalize(glm::vec3(0.3f, 0.6f, 0.4f)), 
        3.0f                                            
    );
    skyAtmo.effectData.data2 = glm::vec4(
        3.5f,   
        1.2f,   
        0.8f,   
        0.0f    
    );

    e->backgroundEffects.push_back(gradient);   
    e->backgroundEffects.push_back(skyAtmo);    

    // Use the atmospheric sky by default
    e->currentBackgroundEffect = e->currentBackgroundEffect = (uint32_t)e->backgroundEffects.size() - 1;

   
    e->mainDeletionQueue.push_function([=]() {
        for (auto& effect : e->backgroundEffects)
            if (effect.pipeline != VK_NULL_HANDLE)
                vkDestroyPipeline(e->device, effect.pipeline, nullptr);
        if (e->gradientPipelineLayout != VK_NULL_HANDLE)
            vkDestroyPipelineLayout(e->device, e->gradientPipelineLayout, nullptr);
        e->backgroundEffects.clear();
        });

    vkDestroyShaderModule(e->device, computeDrawShader, nullptr);
  
    vkDestroyShaderModule(e->device, skyAtmoShader, nullptr);

    LOG("Background pipelines created: gradient | sky | sky_atmo");
}

void init_mesh_pipelines(Engine* e)
{
    LOG("Building mesh pipeline...");
    VkShaderModule meshVertShader;
    if (!e->util.load_shader_module("shaders/colored_triangle_mesh.vert.spv", e->device, &meshVertShader)) {
        LOG_ERROR("Failed to load vertex shader");
        std::exit(1);
    }

    VkShaderModule meshFragShader;
    if (!e->util.load_shader_module("shaders/tex_image.frag.spv", e->device, &meshFragShader)) {
        LOG_ERROR("Failed to load fragment shader");
        std::exit(1);
    }

    VkPushConstantRange pushRange{};
    pushRange.offset = 0;
    pushRange.size = sizeof(MeshPushConstants);
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    VkPipelineLayoutCreateInfo layoutInfo = e->util.pipeline_layout_create_info();
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &e->bindlessLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;

    VK_CHECK(vkCreatePipelineLayout(e->device, &layoutInfo, nullptr, &e->meshPipelineLayout));

  
    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = sizeof(Vertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::vector<VkVertexInputAttributeDescription> attributes = {
        { 0, 0, VK_FORMAT_R32G32B32_SFLOAT,    (uint32_t)offsetof(Vertex, position) },
        { 1, 0, VK_FORMAT_R32G32_SFLOAT,        (uint32_t)offsetof(Vertex, uv)       },
        { 2, 0, VK_FORMAT_R32G32B32_SFLOAT,    (uint32_t)offsetof(Vertex, normal)   },
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
    set_cull_mode(VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE, pb);
	set_multisampling(e->msaaSamples, pb);
    enable_blending_alphablend(pb);
    enable_depthtest(pb, VK_COMPARE_OP_LESS_OR_EQUAL);
    set_color_attachment_format(e->drawImage.imageFormat, pb);
    set_depth_format(e->depthImage.imageFormat, pb);

    pb.vertexInputInfo = vertexInputInfo;
    pb.pipelineLayout = e->meshPipelineLayout;

    e->meshPipeline = build_pipeline(e->device, pb);
    if (e->meshPipeline == VK_NULL_HANDLE) {
        LOG_ERROR("Failed to create mesh pipeline");
        std::exit(1);
    }

    vkDestroyShaderModule(e->device, meshVertShader, nullptr);
    vkDestroyShaderModule(e->device, meshFragShader, nullptr);

    e->mainDeletionQueue.push_function([=]() {
        vkDestroyPipelineLayout(e->device, e->meshPipelineLayout, nullptr);
        vkDestroyPipeline(e->device, e->meshPipeline, nullptr);
        });

    LOG("Mesh pipeline created");
}

void init_shadow_pipeline(Engine* e)
{
    VkShaderModule shadowVertShader;
    if (!e->util.load_shader_module("shaders/shadow.vert.spv", e->device, &shadowVertShader)) {
        LOG_ERROR("Failed to load shadow.vert.spv");
        std::exit(1);
    }

    VkPushConstantRange pushRange{};
    pushRange.offset = 0;
    pushRange.size = sizeof(ShadowPushConstants);  
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;   

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 0;       
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;

    VK_CHECK(vkCreatePipelineLayout(e->device, &layoutInfo, nullptr,
        &e->shadowPipelineLayout));

    
    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = sizeof(Vertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    
    VkVertexInputAttributeDescription posAttr{};
    posAttr.location = 0;
    posAttr.binding = 0;
    posAttr.format = VK_FORMAT_R32G32B32_SFLOAT;
    posAttr.offset = offsetof(Vertex, position);

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount = 1;     
    vertexInput.pVertexAttributeDescriptions = &posAttr;

	


    PipelineBuilder pb;
    set_shaders(shadowVertShader, VK_NULL_HANDLE, pb);   
    set_input_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, pb);
    set_polygon_mode(VK_POLYGON_MODE_FILL, pb);

    set_cull_mode(VK_CULL_MODE_FRONT_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE, pb);

    set_multisampling_none(pb);
    enable_depthtest(pb, VK_COMPARE_OP_LESS_OR_EQUAL);

    
    set_depth_format(e->shadowMapImage.imageFormat, pb);

    pb.vertexInputInfo = vertexInput;
    pb.pipelineLayout = e->shadowPipelineLayout;

    e->shadowPipeline = build_pipeline(e->device, pb);
    if (e->shadowPipeline == VK_NULL_HANDLE) {
        LOG_ERROR("Failed to create shadow pipeline");
        std::exit(1);
    }

    vkDestroyShaderModule(e->device, shadowVertShader, nullptr);

    e->mainDeletionQueue.push_function([=]() {
        vkDestroyPipelineLayout(e->device, e->shadowPipelineLayout, nullptr);
        vkDestroyPipeline(e->device, e->shadowPipeline, nullptr);
        });

    LOG("Shadow pipeline created");
}