// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/impl/temp_frame_renderer.h"

#include "escher/impl/mesh_impl.h"
#include "escher/impl/mesh_manager.h"
#include "escher/impl/vulkan_utils.h"

namespace escher {
namespace impl {

namespace {

// Temporary placeholder.
struct ColorVertex {
  vec2 position;
  vec3 color;
};

constexpr char g_vertex_src[] = R"GLSL(
  #version 450
  #extension GL_ARB_separate_shader_objects : enable

  layout(location = 0) in vec2 inPosition;
  layout(location = 1) in vec3 inColor;

  layout(location = 0) out vec3 fragColor;

  out gl_PerVertex {
      vec4 gl_Position;
  };

  void main() {
      // Halfway between min and max depth.
      gl_Position = vec4(inPosition, 0.5, 1.0);
      fragColor = inColor;
  }
  )GLSL";

constexpr char g_fragment_src[] = R"GLSL(
  #version 450
  #extension GL_ARB_separate_shader_objects : enable

  layout(location = 0) in vec3 inColor;

  layout(location = 0) out vec4 outColor;

  void main() {
      outColor = vec4(inColor, 1.0);
  }
  )GLSL";

}  // namespace

TempFrameRenderer::TempFrameRenderer(const VulkanContext& context,
                                     MeshManager* mesh_manager,
                                     vk::RenderPass render_pass)
    : context_(context),
      mesh_manager_(mesh_manager),
      render_pass_(render_pass) {
  triangle_ = CreateTriangle();

  vk::Device device = context.device;

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
        ESCHER_CHECKED_VK_RESULT(device.createShaderModule(module_info));
  }

  vk::ShaderModule fragment_module;
  {
    SpirvData spirv = fragment_spirv_future.get();

    vk::ShaderModuleCreateInfo module_info;
    module_info.codeSize = spirv.size() * sizeof(uint32_t);
    module_info.pCode = spirv.data();
    fragment_module =
        ESCHER_CHECKED_VK_RESULT(device.createShaderModule(module_info));
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
  vertex_input_info.pVertexBindingDescriptions = &(triangle_->spec.binding);
  vertex_input_info.vertexAttributeDescriptionCount =
      triangle_->spec.attributes.size();
  vertex_input_info.pVertexAttributeDescriptions =
      triangle_->spec.attributes.data();

  vk::PipelineInputAssemblyStateCreateInfo input_assembly_info;
  input_assembly_info.topology = vk::PrimitiveTopology::eTriangleList;
  input_assembly_info.primitiveRestartEnable = false;

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

  // TODO: we'll need to push uniforms into UBOs, and add appropriate layouts.
  vk::PipelineLayoutCreateInfo pipeline_layout_info;
  pipeline_layout_info.setLayoutCount = 0;
  pipeline_layout_info.pushConstantRangeCount = 0;

  pipeline_layout_ = ESCHER_CHECKED_VK_RESULT(
      device.createPipelineLayout(pipeline_layout_info, nullptr));

  vk::GraphicsPipelineCreateInfo pipeline_info;
  pipeline_info.stageCount = 2;
  pipeline_info.pStages = shader_stages;
  pipeline_info.pVertexInputState = &vertex_input_info;
  pipeline_info.pInputAssemblyState = &input_assembly_info;
  pipeline_info.pViewportState = &viewport_state;
  pipeline_info.pRasterizationState = &rasterizer;
  pipeline_info.pMultisampleState = &multisampling;
  pipeline_info.pColorBlendState = &color_blending;
  pipeline_info.layout = pipeline_layout_;
  pipeline_info.renderPass = render_pass;
  pipeline_info.subpass = 0;
  pipeline_info.basePipelineHandle = VK_NULL_HANDLE;

  pipeline_ = ESCHER_CHECKED_VK_RESULT(
      device.createGraphicsPipeline(nullptr, pipeline_info));

  device.destroyShaderModule(vertex_module);
  device.destroyShaderModule(fragment_module);
}

TempFrameRenderer::~TempFrameRenderer() {
  context_.device.destroyPipeline(pipeline_);

  // TODO: can this be done earlier, as soon as all pipelines that need it have
  // been created?  Or, because it is a handle, does the pipeline expect it to
  // be alive?
  context_.device.destroyPipelineLayout(pipeline_layout_);
}

vk::Result TempFrameRenderer::Render(RenderContext::Frame* frame,
                                     vk::Framebuffer framebuffer) {
  FTL_LOG(INFO) << "rendering frame #" << frame->frame_number();

  auto result =
      frame->AllocateCommandBuffers(1, vk::CommandBufferLevel::ePrimary);
  if (result.result != vk::Result::eSuccess) {
    FTL_LOG(WARNING) << "failed to allocated CommandBuffer : "
                     << to_string(result.result);
    return result.result;
  }
  auto command_buffer = result.value[0];

  vk::ClearValue clear_values[2];
  clear_values[0] =
      vk::ClearColorValue(std::array<float, 4>{{0.f, 1.f, 0.f, 1.f}});
  clear_values[1] = vk::ClearDepthStencilValue{1.f, 0};

  static constexpr uint32_t kWidth = 1024;
  static constexpr uint32_t kHeight = 1024;

  vk::RenderPassBeginInfo render_pass_begin;
  render_pass_begin.renderPass = render_pass_;
  render_pass_begin.renderArea.offset.x = 0;
  render_pass_begin.renderArea.offset.y = 0;
  // TODO: pull these from somewhere
  render_pass_begin.renderArea.extent.width = kWidth;
  render_pass_begin.renderArea.extent.height = kHeight;
  render_pass_begin.clearValueCount = 2;
  render_pass_begin.pClearValues = clear_values;
  render_pass_begin.framebuffer = framebuffer;

  command_buffer.beginRenderPass(&render_pass_begin,
                                 vk::SubpassContents::eInline);

  vk::Viewport viewport;
  viewport.width = static_cast<float>(kWidth);
  viewport.height = static_cast<float>(kHeight);
  viewport.minDepth = static_cast<float>(0.0f);
  viewport.maxDepth = static_cast<float>(1.0f);
  command_buffer.setViewport(0, 1, &viewport);

  vk::Rect2D scissor;
  scissor.extent.width = kWidth;
  scissor.extent.height = kHeight;
  scissor.offset.x = 0;
  scissor.offset.y = 0;
  command_buffer.setScissor(0, 1, &scissor);

  command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline_);

  // TODO: The following two lines are clumsy.
  auto mesh = static_cast<MeshImpl*>(triangle_.get());
  frame->AddWaitSemaphore(mesh->Draw(command_buffer, frame->frame_number()));

  if (frame->frame_number() % 100 == 0) {
    // Every hundredth frame, replace the mesh with another one, just to make
    // sure that our keep-mesh-alive-until-frame-is-done-rendering code works
    // properly.
    FTL_DLOG(INFO) << "Recreating triangle mesh in frame: "
                   << frame->frame_number();
    triangle_ = CreateTriangle();
  }

  command_buffer.endRenderPass();
  return command_buffer.end();
}

MeshPtr TempFrameRenderer::CreateTriangle() {
  MeshSpec spec;
  spec.binding.binding = 0;
  spec.binding.stride = sizeof(ColorVertex);
  spec.binding.inputRate = vk::VertexInputRate::eVertex;
  vk::VertexInputAttributeDescription attribute;
  // Configure "position" attribute.
  attribute.location = 0;
  attribute.binding = 0;
  attribute.format = vk::Format::eR32G32Sfloat;
  attribute.offset = offsetof(ColorVertex, position);
  spec.attributes.push_back(attribute);
  spec.attribute_names.push_back("position");
  // Configure "color" attribute.
  attribute.location = 1;
  attribute.binding = 0;
  attribute.format = vk::Format::eR32G32B32Sfloat;
  attribute.offset = offsetof(ColorVertex, color);
  spec.attributes.push_back(attribute);
  spec.attribute_names.push_back("color");

  ColorVertex v0{vec2(-0.5, -0.5), vec3(1.0, 0.0, 0.0)};
  ColorVertex v1{vec2(0.5, 0.5), vec3(0.0, 0.0, 1.0)};
  ColorVertex v2{vec2(-0.5, 0.5), vec3(0.0, 1.0, 0.0)};

  MeshBuilderPtr builder = mesh_manager_->NewMeshBuilder(spec, 6, 12);
  return builder->AddVertex(v0)
      .AddVertex(v1)
      .AddVertex(v2)
      .AddIndex(0)
      .AddIndex(1)
      .AddIndex(2)
      .Build();
}

}  // namespace impl
}  // namespace escher
