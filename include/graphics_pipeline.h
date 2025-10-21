#pragma once

#include <vector>
#include <vulkan/vulkan_core.h>

struct PipelineBuilder
{
  std::vector<VkPipelineShaderStageCreateInfo> shaderStages;

  VkPipelineInputAssemblyStateCreateInfo inputAssembly;
  VkPipelineRasterizationStateCreateInfo rasterizer;
  VkPipelineColorBlendAttachmentState colorBlendAttachment;
  VkPipelineMultisampleStateCreateInfo multisampling;
  VkPipelineLayout pipelineLayout;
  VkPipelineRenderingCreateInfo renderInfo;
  VkFormat colorAttachmentformat;
  VkPipelineDepthStencilStateCreateInfo depthStencil; 

  PipelineBuilder();
   
};

void set_shaders(VkShaderModule vertexShader, VkShaderModule fragmentShader,PipelineBuilder& pb);
void clear(PipelineBuilder& pb);
VkPipeline build_pipeline(VkDevice device,PipelineBuilder& pb);
void set_input_topology(VkPrimitiveTopology topology,PipelineBuilder& pb);
void set_polygon_mode(VkPolygonMode mode, PipelineBuilder& pb);
void set_cull_mode(VkCullModeFlags cullMode, VkFrontFace frontFace, PipelineBuilder& pb);
void set_multisampling_none(PipelineBuilder& pb); 
void set_color_attachment_format(VkFormat format, PipelineBuilder& pb); 
void disable_blending(PipelineBuilder& pb); 
void set_depth_format(VkFormat format, PipelineBuilder& pb); 
void disable_depthtest(PipelineBuilder& pb);
void enable_blending_additive(PipelineBuilder& pb);
void enable_blending_alphablend(PipelineBuilder& pb);

