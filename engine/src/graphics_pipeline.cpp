#include "graphics_pipeline.h"
#include "iostream"
#include "helper.h"
#include <vulkan/vulkan_core.h>

Utils utils;

// Constructor for the PipelineBuilder to ensure all structs are properly initialized.
PipelineBuilder::PipelineBuilder()
{
    // Zero-initialize all structs to avoid garbage values
    inputAssembly = {};
    rasterizer = {};
    colorBlendAttachment = {};
    multisampling = {};
    depthStencil = {};
    renderInfo = {};

    // Set sType for all structs that have one
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    renderInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;

    // Set other sensible defaults
    rasterizer.lineWidth = 1.0f;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;
}

// You can now delete your `clear` function.

VkPipeline build_pipeline(VkDevice device, PipelineBuilder& pb)
{
    // Viewport state
    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    // Color blending
    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &pb.colorBlendAttachment;

    // Vertex input
    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    // Dynamic states
    VkDynamicState states[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamicInfo{};
    dynamicInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicInfo.pDynamicStates = states;
    dynamicInfo.dynamicStateCount = 2;

    // Final pipeline info
    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.pNext = &pb.renderInfo;
    pipelineInfo.stageCount = static_cast<uint32_t>(pb.shaderStages.size());
    pipelineInfo.pStages = pb.shaderStages.data();
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &pb.inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &pb.rasterizer;
    pipelineInfo.pMultisampleState = &pb.multisampling;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDepthStencilState = &pb.depthStencil;
    pipelineInfo.pDynamicState = &dynamicInfo;
    pipelineInfo.layout = pb.pipelineLayout;
    pipelineInfo.pVertexInputState = &pb.vertexInputInfo;
    // Debug logs
    if (pb.pipelineLayout == VK_NULL_HANDLE) {
        std::cerr << "❌ Pipeline layout is NULL!" << std::endl;
    }
    std::cout << "Creating pipeline with " << pb.shaderStages.size() << " shader stages" << std::endl;

    VkPipeline newPipeline = VK_NULL_HANDLE;
    VkResult result = vkCreateGraphicsPipelines(
        device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &newPipeline);

    if (result != VK_SUCCESS) {
        std::cerr << "❌ vkCreateGraphicsPipelines failed with error code: " << result << std::endl;
        return VK_NULL_HANDLE;
    }

    std::cout << "✅ Graphics pipeline created successfully!" << std::endl;
    return newPipeline;
}

void set_shaders(VkShaderModule vertexShader, VkShaderModule fragmentShader, PipelineBuilder& pb)
{
    pb.shaderStages.clear();
    const char* entry = "main";
    pb.shaderStages.push_back(utils.pipeline_shader_stage_create_info(VK_SHADER_STAGE_VERTEX_BIT, vertexShader, entry));
    pb.shaderStages.push_back(utils.pipeline_shader_stage_create_info(VK_SHADER_STAGE_FRAGMENT_BIT, fragmentShader, entry));
}

void set_input_topology(VkPrimitiveTopology topology, PipelineBuilder& pb)
{
    pb.inputAssembly.topology = topology;
    pb.inputAssembly.primitiveRestartEnable = VK_FALSE;
}

void set_polygon_mode(VkPolygonMode mode, PipelineBuilder& pb)
{
    pb.rasterizer.polygonMode = mode;
    pb.rasterizer.lineWidth = 1.f;
}

void set_cull_mode(VkCullModeFlags cullMode, VkFrontFace frontFace, PipelineBuilder& pb)
{
    pb.rasterizer.cullMode = cullMode;
    pb.rasterizer.frontFace = frontFace;
}

void set_multisampling_none(PipelineBuilder& pb)
{
    pb.multisampling.sampleShadingEnable = VK_FALSE;
    pb.multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    pb.multisampling.minSampleShading = 1.0f;
    pb.multisampling.pSampleMask = nullptr;
    pb.multisampling.alphaToCoverageEnable = VK_FALSE;
    pb.multisampling.alphaToOneEnable = VK_FALSE;
}

void set_color_attachment_format(VkFormat format, PipelineBuilder& pb)
{
    pb.colorAttachmentformat = format;
    pb.renderInfo.colorAttachmentCount = 1;
    pb.renderInfo.pColorAttachmentFormats = &pb.colorAttachmentformat;
}

void disable_blending(PipelineBuilder& pb)
{
    pb.colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_A_BIT;
    pb.colorBlendAttachment.blendEnable = VK_FALSE;
}

void set_depth_format(VkFormat format, PipelineBuilder& pb)

{
    pb.renderInfo.depthAttachmentFormat = format;
}

void disable_depthtest(PipelineBuilder& pb)
{
    pb.depthStencil.depthTestEnable = VK_FALSE;
    pb.depthStencil.depthWriteEnable = VK_FALSE;
    pb.depthStencil.depthCompareOp = VK_COMPARE_OP_NEVER;
    pb.depthStencil.depthBoundsTestEnable = VK_FALSE;
    pb.depthStencil.front = {};
    pb.depthStencil.back = {};
    pb.depthStencil.minDepthBounds = 0.f;
    pb.depthStencil.maxDepthBounds = 1.0f;
}

void enable_depthtest(PipelineBuilder& pb, VkCompareOp compareOp)
{
    pb.depthStencil.depthTestEnable = VK_TRUE;
    pb.depthStencil.depthWriteEnable = VK_TRUE;
    pb.depthStencil.depthCompareOp = compareOp;
    pb.depthStencil.depthBoundsTestEnable = VK_FALSE;
    pb.depthStencil.front = {};
    pb.depthStencil.back = {};
    pb.depthStencil.minDepthBounds = 0.f;
    pb.depthStencil.maxDepthBounds = 1.0f;
	pb.depthStencil.stencilTestEnable = VK_FALSE;
}

void enable_blending_additive(PipelineBuilder& pb)
{
    pb.colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    pb.colorBlendAttachment.blendEnable = VK_TRUE;
    pb.colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    pb.colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
    pb.colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    pb.colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    pb.colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    pb.colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
}

void enable_blending_alphablend(PipelineBuilder& pb)
{
    pb.colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    pb.colorBlendAttachment.blendEnable = VK_TRUE;
    pb.colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    pb.colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    pb.colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    pb.colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    pb.colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    pb.colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
}
