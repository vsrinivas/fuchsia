// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/impl/model_pipeline_cache.h"

#include "escher/geometry/types.h"
// TODO: move MeshSpecImpl into its own file, then remove this.
#include "escher/impl/mesh_impl.h"
#include "escher/impl/model_data.h"
#include "escher/impl/model_pipeline.h"
#include "escher/impl/vulkan_utils.h"

namespace escher {
namespace impl {

namespace {

constexpr char g_vertex_src[] = R"GLSL(
  #version 450
  #extension GL_ARB_separate_shader_objects : enable

  layout(location = 0) in vec2 inPosition;
  layout(location = 2) in vec2 inUV;

  layout(location = 0) out vec2 fragUV;

  layout(set = 1, binding = 0) uniform PerObject {
    mat4 transform;
    vec4 color;
  };

  out gl_PerVertex {
    vec4 gl_Position;
  };

  void main() {
    // Halfway between min and max depth.
    gl_Position = transform * vec4(inPosition, 0, 1);
    // Divide by 25 to convert 'Material Stage' depth to range 0-1.
    gl_Position.z *= 0.04;
    fragUV = inUV;
  }
  )GLSL";

constexpr char g_fragment_src[] = R"GLSL(
  #version 450
  #extension GL_ARB_separate_shader_objects : enable

  layout(location = 0) in vec2 inUV;

  layout(set = 0, binding = 0) uniform PerModel {
    vec4 brightness;
  };

  layout(set = 1, binding = 0) uniform PerObject {
    mat4 transform;
    vec4 color;
  };

  layout(set = 1, binding = 1) uniform sampler2D tex;

  layout(location = 0) out vec4 outColor;

  void main() {
    outColor = brightness * color * texture(tex, inUV);
  }

  )GLSL";

}  // namespace

ModelPipelineCache::ModelPipelineCache(vk::Device device,
                                       vk::RenderPass depth_prepass,
                                       vk::RenderPass lighting_pass)
    : device_(device),
      depth_prepass_(depth_prepass),
      lighting_pass_(lighting_pass) {}

ModelPipelineCacheOLD::ModelPipelineCacheOLD(vk::Device device,
                                             vk::RenderPass depth_prepass,
                                             vk::RenderPass lighting_pass,
                                             ModelData* model_data)
    : ModelPipelineCache(device, depth_prepass, lighting_pass),
      model_data_(model_data) {}

ModelPipelineCacheOLD::~ModelPipelineCacheOLD() {
  device_.waitIdle();
  pipelines_.clear();
}

ModelPipeline* ModelPipelineCacheOLD::GetPipeline(
    const ModelPipelineSpec& spec,
    const MeshSpecImpl& mesh_spec_impl) {
  // TODO: deal with hash collisions
  auto it = pipelines_.find(spec);
  if (it != pipelines_.end()) {
    return it->second.get();
  }
  auto new_pipeline = NewPipeline(spec, mesh_spec_impl);
  auto new_pipeline_ptr = new_pipeline.get();
  pipelines_[spec] = std::move(new_pipeline);
  return new_pipeline_ptr;
}

std::unique_ptr<ModelPipeline> ModelPipelineCacheOLD::NewPipeline(
    const ModelPipelineSpec& spec,
    const MeshSpecImpl& mesh_spec_impl) {
  // TODO: create customized pipelines for different shapes/materials/etc.

  auto vertex_spirv_future =
      compiler_.Compile(vk::ShaderStageFlagBits::eVertex, {{g_vertex_src}},
                        std::string(), "main");
  auto fragment_spirv_future =
      compiler_.Compile(vk::ShaderStageFlagBits::eFragment, {{g_fragment_src}},
                        std::string(), "main");

  vk::ShaderModule vertex_module;
  {
    SpirvData spirv = vertex_spirv_future.get();

    vk::ShaderModuleCreateInfo module_info;
    module_info.codeSize = spirv.size() * sizeof(uint32_t);
    module_info.pCode = spirv.data();
    vertex_module =
        ESCHER_CHECKED_VK_RESULT(device_.createShaderModule(module_info));
  }

  vk::ShaderModule fragment_module;
  {
    SpirvData spirv = fragment_spirv_future.get();

    vk::ShaderModuleCreateInfo module_info;
    module_info.codeSize = spirv.size() * sizeof(uint32_t);
    module_info.pCode = spirv.data();
    fragment_module =
        ESCHER_CHECKED_VK_RESULT(device_.createShaderModule(module_info));
  }

  vk::PipelineShaderStageCreateInfo vertex_stage_info;
  vertex_stage_info.stage = vk::ShaderStageFlagBits::eVertex;
  vertex_stage_info.module = vertex_module;
  vertex_stage_info.pName = "main";

  vk::PipelineShaderStageCreateInfo fragment_stage_info;
  fragment_stage_info.stage = vk::ShaderStageFlagBits::eFragment;
  fragment_stage_info.module = fragment_module;
  fragment_stage_info.pName = "main";

  vk::PipelineShaderStageCreateInfo shader_stages[] = {vertex_stage_info,
                                                       fragment_stage_info};

  vk::PipelineVertexInputStateCreateInfo vertex_input_info;
  vertex_input_info.vertexBindingDescriptionCount = 1;
  vertex_input_info.pVertexBindingDescriptions = &mesh_spec_impl.binding;
  vertex_input_info.vertexAttributeDescriptionCount =
      mesh_spec_impl.attributes.size();
  vertex_input_info.pVertexAttributeDescriptions =
      mesh_spec_impl.attributes.data();

  vk::PipelineInputAssemblyStateCreateInfo input_assembly_info;
  input_assembly_info.topology = vk::PrimitiveTopology::eTriangleList;
  input_assembly_info.primitiveRestartEnable = false;

  vk::PipelineDepthStencilStateCreateInfo depth_stencil_info;
  depth_stencil_info.depthTestEnable = true;
  if (spec.use_depth_prepass) {
    // The whole point of a depth pre-pass is to write the depth buffer.
    depth_stencil_info.depthWriteEnable = true;
    depth_stencil_info.depthCompareOp = vk::CompareOp::eLess;
  } else {
    // Don't write to the depth buffer; we re-use the buffer generated in the
    // depth pre-pass.  Must modify the comparison operator, otherwise nothing
    // would appear, because the nearest objects would have the exact same depth
    // as in the depth buffer.
    depth_stencil_info.depthWriteEnable = true;
    depth_stencil_info.depthCompareOp = vk::CompareOp::eLessOrEqual;
  }
  depth_stencil_info.depthBoundsTestEnable = false;
  depth_stencil_info.stencilTestEnable = false;

  // TODO: make viewport a dynamic pipeline property that is updated each frame.
  vk::Viewport viewport;
  viewport.x = 0.0f;
  viewport.y = 0.0f;
  viewport.width = 1024.f;
  viewport.height = 1024.f;
  viewport.minDepth = 0.0f;
  viewport.maxDepth = 1.0f;

  // TODO: make viewport a dynamic pipeline property that is updated each frame.
  vk::Rect2D scissor;
  scissor.offset = vk::Offset2D{0, 0};
  scissor.extent = vk::Extent2D{1024, 1024};

  vk::PipelineViewportStateCreateInfo viewport_state;
  viewport_state.viewportCount = 1;
  viewport_state.pViewports = &viewport;
  viewport_state.scissorCount = 1;
  viewport_state.pScissors = &scissor;

  vk::PipelineRasterizationStateCreateInfo rasterizer;
  rasterizer.depthClampEnable = false;
  rasterizer.rasterizerDiscardEnable = false;
  rasterizer.polygonMode = vk::PolygonMode::eFill;
  rasterizer.lineWidth = 1.0f;
  rasterizer.cullMode = vk::CullModeFlagBits::eBack;
  rasterizer.frontFace = vk::FrontFace::eClockwise;
  rasterizer.depthBiasEnable = false;

  vk::PipelineMultisampleStateCreateInfo multisampling;
  multisampling.sampleShadingEnable = false;
  multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;

  vk::PipelineColorBlendAttachmentState color_blend_attachment;
  color_blend_attachment.colorWriteMask =
      vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
      vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
  color_blend_attachment.blendEnable = false;

  vk::PipelineColorBlendStateCreateInfo color_blending;
  color_blending.logicOpEnable = false;
  color_blending.logicOp = vk::LogicOp::eCopy;
  color_blending.attachmentCount = 1;
  color_blending.pAttachments = &color_blend_attachment;
  color_blending.blendConstants[0] = 0.0f;
  color_blending.blendConstants[1] = 0.0f;
  color_blending.blendConstants[2] = 0.0f;
  color_blending.blendConstants[3] = 0.0f;

  vk::DescriptorSetLayout layouts[] = {model_data_->per_model_layout(),
                                       model_data_->per_object_layout()};
  vk::PipelineLayoutCreateInfo pipeline_layout_info;
  pipeline_layout_info.setLayoutCount = 2;
  pipeline_layout_info.pSetLayouts = layouts;
  pipeline_layout_info.pushConstantRangeCount = 0;

  vk::PipelineLayout pipeline_layout = ESCHER_CHECKED_VK_RESULT(
      device_.createPipelineLayout(pipeline_layout_info, nullptr));

  vk::GraphicsPipelineCreateInfo pipeline_info;
  pipeline_info.stageCount = 2;
  pipeline_info.pStages = shader_stages;
  pipeline_info.pVertexInputState = &vertex_input_info;
  pipeline_info.pInputAssemblyState = &input_assembly_info;
  pipeline_info.pViewportState = &viewport_state;
  pipeline_info.pRasterizationState = &rasterizer;
  pipeline_info.pDepthStencilState = &depth_stencil_info;
  pipeline_info.pMultisampleState = &multisampling;
  pipeline_info.pColorBlendState = &color_blending;
  pipeline_info.layout = pipeline_layout;
  pipeline_info.renderPass = lighting_pass_;
  pipeline_info.subpass = 0;
  pipeline_info.basePipelineHandle = VK_NULL_HANDLE;

  vk::Pipeline pipeline = ESCHER_CHECKED_VK_RESULT(
      device_.createGraphicsPipeline(nullptr, pipeline_info));

  device_.destroyShaderModule(vertex_module);
  device_.destroyShaderModule(fragment_module);

  return std::make_unique<ModelPipeline>(spec, device_, pipeline,
                                         pipeline_layout);
}

}  // namespace impl
}  // namespace escher
