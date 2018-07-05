// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/impl/model_pipeline_cache.h"

#include "lib/escher/geometry/types.h"
#include "lib/escher/impl/glsl_compiler.h"
#include "lib/escher/impl/mesh_shader_binding.h"
#include "lib/escher/impl/model_data.h"
#include "lib/escher/impl/model_pipeline.h"
#include "lib/escher/impl/model_render_pass.h"
#include "lib/escher/impl/vulkan_utils.h"
#include "lib/escher/resources/resource_recycler.h"
#include "lib/escher/util/trace_macros.h"

namespace escher {
namespace impl {

const ResourceTypeInfo ModelPipelineCache::kTypeInfo(
    "ModelPipelineCache", ResourceType::kResource,
    ResourceType::kImplModelPipelineCache);

ModelPipelineCache::ModelPipelineCache(ResourceRecycler* recycler,
                                       ModelDataPtr model_data,
                                       ModelRenderPass* render_pass)
    : Resource(recycler),
      model_data_(std::move(model_data)),
      render_pass_(render_pass),
      compiler_(std::make_unique<GlslToSpirvCompiler>()) {
  FXL_DCHECK(model_data_);
  FXL_DCHECK(render_pass_);
}

ModelPipelineCache::~ModelPipelineCache() { pipelines_.clear(); }

ModelPipeline* ModelPipelineCache::GetPipeline(const ModelPipelineSpec& spec) {
  auto it = pipelines_.find(spec);
  if (it != pipelines_.end()) {
    return it->second.get();
  }
  auto new_pipeline = NewPipeline(spec);
  auto new_pipeline_ptr = new_pipeline.get();
  pipelines_[spec] = std::move(new_pipeline);
  return new_pipeline_ptr;
}

namespace {

// Creates a new PipelineLayout and Pipeline using only the provided arguments.
std::pair<vk::Pipeline, vk::PipelineLayout> NewPipelineHelper(
    ModelData* model_data, vk::ShaderModule vertex_module,
    vk::ShaderModule fragment_module, bool enable_depth_test,
    bool enable_depth_write, bool enable_blending,
    vk::CompareOp depth_compare_op, vk::RenderPass render_pass,
    std::vector<vk::DescriptorSetLayout> descriptor_set_layouts,
    const ModelPipelineSpec& spec, vk::SampleCountFlagBits sample_count) {
  vk::Device device = model_data->device();

  // Depending on configuration, more dynamic states may be added later.
  vk::PipelineDynamicStateCreateInfo dynamic_state_info;
  std::vector<vk::DynamicState> dynamic_states{vk::DynamicState::eViewport,
                                               vk::DynamicState::eScissor};

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
  {
    auto& mesh_shader_binding =
        model_data->GetMeshShaderBinding(spec.mesh_spec);
    vertex_input_info.vertexBindingDescriptionCount = 1;
    vertex_input_info.pVertexBindingDescriptions =
        mesh_shader_binding.binding();
    vertex_input_info.vertexAttributeDescriptionCount =
        mesh_shader_binding.attributes().size();
    vertex_input_info.pVertexAttributeDescriptions =
        mesh_shader_binding.attributes().data();
  }

  vk::PipelineInputAssemblyStateCreateInfo input_assembly_info;
  input_assembly_info.topology = vk::PrimitiveTopology::eTriangleList;
  input_assembly_info.primitiveRestartEnable = false;

  vk::PipelineDepthStencilStateCreateInfo depth_stencil_info;
  depth_stencil_info.depthTestEnable = enable_depth_test;
  depth_stencil_info.depthWriteEnable = enable_depth_write;
  depth_stencil_info.depthCompareOp = depth_compare_op;
  depth_stencil_info.depthBoundsTestEnable = false;
  // Set the stencil state appropriately, depending on whether we (i.e. the
  // escher::Object eventually rendered by this pipeline) is a clipper and/or
  // a clippee.  See also ModelDisplayListBuilder, where these pipelines are
  // used.
  depth_stencil_info.stencilTestEnable = true;
  auto& op_state = depth_stencil_info.front;
  op_state.compareMask = 0xFF;
  op_state.writeMask = 0xFF;
  if (!spec.is_clippee) {
    switch (spec.clipper_state) {
      case ModelPipelineSpec::ClipperState::kNoClipChildren: {
        // We neither clip nor are clipped, so we can disable the stencil test
        // for this pipeline.
        depth_stencil_info.stencilTestEnable = false;
      } break;
      case ModelPipelineSpec::ClipperState::kBeginClipChildren: {
        // We are a top-level clipper that is not clipped by anyone else.
        // Write to the stencil buffer to define where children are allowed
        // to draw.
        op_state.failOp = vk::StencilOp::eKeep;
        op_state.passOp = vk::StencilOp::eReplace;
        op_state.depthFailOp = vk::StencilOp::eReplace;
        op_state.compareOp = vk::CompareOp::eAlways;
        op_state.reference = 1;
      } break;
      case ModelPipelineSpec::ClipperState::kEndClipChildren: {
        // We are a top-level clipper that is not clipped by anyone else.
        // Clean up stencil buffer so that we do not clip subsequent objects.
        op_state.failOp = vk::StencilOp::eKeep;
        op_state.passOp = vk::StencilOp::eReplace;
        op_state.depthFailOp = vk::StencilOp::eReplace;
        op_state.compareOp = vk::CompareOp::eAlways;
        op_state.reference = 0;
      } break;
    }
  } else {
    // In all cases where we are clipped by another object, we must be able
    // to dynamically set the stencil reference value.
    dynamic_states.push_back(vk::DynamicState::eStencilReference);

    switch (spec.clipper_state) {
      case ModelPipelineSpec::ClipperState::kNoClipChildren: {
        // We are clipped by some other object, but do not clip any children.
        // Therefore, test the stencil buffer, but do not update it.
        op_state.failOp = vk::StencilOp::eKeep;
        op_state.passOp = vk::StencilOp::eKeep;
        op_state.depthFailOp = vk::StencilOp::eKeep;
        op_state.compareOp = vk::CompareOp::eEqual;
      } break;
      case ModelPipelineSpec::ClipperState::kBeginClipChildren: {
        // We are clipped by some other object, and also want to clip our
        // children.  This is achieved by incrementing the stencil buffer.
        // Therefore, test the stencil buffer, and update it if successful.
        op_state.failOp = vk::StencilOp::eKeep;
        op_state.passOp = vk::StencilOp::eIncrementAndWrap;
        op_state.depthFailOp = vk::StencilOp::eIncrementAndWrap;
        op_state.compareOp = vk::CompareOp::eEqual;
      } break;
      case ModelPipelineSpec::ClipperState::kEndClipChildren: {
        // We have finished clipping our children.  Revert the stencil buffer to
        // its previous state so that we don't clip subsequent objects.
        op_state.failOp = vk::StencilOp::eKeep;
        op_state.passOp = vk::StencilOp::eDecrementAndWrap;
        op_state.depthFailOp = vk::StencilOp::eDecrementAndWrap;
        op_state.compareOp = vk::CompareOp::eEqual;
      } break;
    }
  }

  // This is set dynamically during rendering.
  vk::Viewport viewport;
  viewport.x = 0.0f;
  viewport.y = 0.0f;
  viewport.width = 0.f;
  viewport.height = 0.f;
  viewport.minDepth = 0.0f;
  viewport.maxDepth = 0.0f;

  // This is set dynamically during rendering.
  vk::Rect2D scissor;
  scissor.offset = vk::Offset2D{0, 0};
  scissor.extent = vk::Extent2D{0, 0};

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
  multisampling.rasterizationSamples = sample_count;

  vk::PipelineColorBlendAttachmentState color_blend_attachment;
  if (fragment_module) {
    color_blend_attachment.colorWriteMask =
        vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
        vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
  }
  if (enable_blending) {
    // TODO(ES-28): In some cases we have a constant alpha, so we could
    // optimize this with eConstantAlpha and eOneMinusConstantAlpha.
    color_blend_attachment.blendEnable = true;
    color_blend_attachment.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
    color_blend_attachment.dstColorBlendFactor =
        vk::BlendFactor::eOneMinusSrcAlpha;
    color_blend_attachment.colorBlendOp = vk::BlendOp::eAdd;
    color_blend_attachment.srcAlphaBlendFactor =
        vk::BlendFactor::eOneMinusDstAlpha;
    color_blend_attachment.dstAlphaBlendFactor = vk::BlendFactor::eOne;
    color_blend_attachment.alphaBlendOp = vk::BlendOp::eAdd;
  } else {
    color_blend_attachment.blendEnable = false;
  }

  vk::PipelineColorBlendStateCreateInfo color_blending;
  color_blending.logicOpEnable = false;
  color_blending.logicOp = vk::LogicOp::eCopy;
  color_blending.attachmentCount = 1;
  color_blending.pAttachments = &color_blend_attachment;
  color_blending.blendConstants[0] = 0.0f;
  color_blending.blendConstants[1] = 0.0f;
  color_blending.blendConstants[2] = 0.0f;
  color_blending.blendConstants[3] = 0.0f;

  vk::PipelineLayoutCreateInfo pipeline_layout_info;
  pipeline_layout_info.setLayoutCount =
      static_cast<uint32_t>(descriptor_set_layouts.size());
  pipeline_layout_info.pSetLayouts = descriptor_set_layouts.data();
  pipeline_layout_info.pushConstantRangeCount = 0;

  vk::PipelineLayout pipeline_layout = ESCHER_CHECKED_VK_RESULT(
      device.createPipelineLayout(pipeline_layout_info, nullptr));

  // All dynamic states have been accumulated, so finalize them.
  dynamic_state_info.dynamicStateCount =
      static_cast<uint32_t>(dynamic_states.size());
  dynamic_state_info.pDynamicStates = dynamic_states.data();

  vk::GraphicsPipelineCreateInfo pipeline_info;
  pipeline_info.stageCount = fragment_module ? 2 : 1;
  pipeline_info.pStages = shader_stages;
  pipeline_info.pVertexInputState = &vertex_input_info;
  pipeline_info.pInputAssemblyState = &input_assembly_info;
  pipeline_info.pViewportState = &viewport_state;
  pipeline_info.pRasterizationState = &rasterizer;
  pipeline_info.pDepthStencilState = &depth_stencil_info;
  pipeline_info.pMultisampleState = &multisampling;
  pipeline_info.pColorBlendState = &color_blending;
  pipeline_info.pDynamicState = &dynamic_state_info;
  pipeline_info.layout = pipeline_layout;
  pipeline_info.renderPass = render_pass;
  pipeline_info.subpass = 0;
  pipeline_info.basePipelineHandle = vk::Pipeline();

  vk::Pipeline pipeline = ESCHER_CHECKED_VK_RESULT(
      device.createGraphicsPipeline(nullptr, pipeline_info));

  return {pipeline, pipeline_layout};
}
}  // namespace

std::unique_ptr<ModelPipeline> ModelPipelineCache::NewPipeline(
    const ModelPipelineSpec& spec) {
  TRACE_DURATION("gfx", "escher::ModelPipelineCache::NewPipeline");
  // TODO: create customized pipelines for different shapes/materials/etc.

  // Only specs with materials may be opaque.
  FXL_DCHECK(!spec.is_opaque || spec.has_material);

  std::future<SpirvData> vertex_spirv_future;
  std::future<SpirvData> fragment_spirv_future;

  vertex_spirv_future = compiler_->Compile(
      vk::ShaderStageFlagBits::eVertex,
      {{render_pass_->GetVertexShaderSourceCode(spec)}}, std::string(), "main");

  // The depth-only pre-pass uses a different renderpass and a cheap fragment
  // shader.
  const bool enable_depth_write = spec.has_material && !spec.disable_depth_test;
  const bool enable_depth_test = !spec.disable_depth_test;
  const bool omit_fragment_shader =
      render_pass_->OmitFragmentShader() || !spec.has_material;
  const bool enable_blending = !spec.is_opaque && !omit_fragment_shader;
  const vk::CompareOp depth_compare_op = vk::CompareOp::eLess;

  if (!omit_fragment_shader) {
    fragment_spirv_future =
        compiler_->Compile(vk::ShaderStageFlagBits::eFragment,
                           {{render_pass_->GetFragmentShaderSourceCode(spec)}},
                           std::string(), "main");
  }

  // Wait for completion of asynchronous shader compilation.
  vk::ShaderModule vertex_module;
  vk::Device device = model_data_->device();
  {
    SpirvData spirv = vertex_spirv_future.get();

    vk::ShaderModuleCreateInfo module_info;
    module_info.codeSize = spirv.size() * sizeof(uint32_t);
    module_info.pCode = spirv.data();
    vertex_module =
        ESCHER_CHECKED_VK_RESULT(device.createShaderModule(module_info));
  }
  vk::ShaderModule fragment_module;
  if (!omit_fragment_shader) {
    SpirvData spirv = fragment_spirv_future.get();

    vk::ShaderModuleCreateInfo module_info;
    module_info.codeSize = spirv.size() * sizeof(uint32_t);
    module_info.pCode = spirv.data();
    fragment_module =
        ESCHER_CHECKED_VK_RESULT(device.createShaderModule(module_info));
  }

  auto pipeline_and_layout = NewPipelineHelper(
      model_data_.get(), vertex_module, fragment_module, enable_depth_test,
      enable_depth_write, enable_blending, depth_compare_op, render_pass_->vk(),
      {model_data_->per_model_layout(), model_data_->per_object_layout()}, spec,
      SampleCountFlagBitsFromInt(render_pass_->sample_count()));

  device.destroyShaderModule(vertex_module);
  if (fragment_module) {
    device.destroyShaderModule(fragment_module);
  }

  return std::make_unique<ModelPipeline>(
      spec, device, pipeline_and_layout.first, pipeline_and_layout.second);
}

}  // namespace impl
}  // namespace escher
