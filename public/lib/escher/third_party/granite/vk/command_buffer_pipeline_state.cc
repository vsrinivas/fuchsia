/* Copyright (c) 2017 Hans-Kristian Arntzen
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

// Based on the following files from the Granite rendering engine:
// - vulkan/command_buffer.cpp

#include "lib/escher/third_party/granite/vk/command_buffer_pipeline_state.h"

#include "lib/escher/impl/vulkan_utils.h"
#include "lib/escher/third_party/granite/vk/pipeline_layout.h"
#include "lib/escher/third_party/granite/vk/render_pass.h"
#include "lib/escher/util/bit_ops.h"
#include "lib/escher/util/enum_cast.h"
#include "lib/escher/util/hasher.h"
#include "lib/escher/util/trace_macros.h"
#include "lib/escher/vk/buffer.h"
#include "lib/escher/vk/shader_program.h"

namespace escher {

#define ASSERT_NUM_STATE_BITS(BIT_COUNT, VALUE_COUNT)                  \
  static_assert(                                                       \
      (1 << CommandBufferPipelineState::StaticState::BIT_COUNT) - 1 >= \
          (VALUE_COUNT - 1),                                           \
      "not enough bits for " #VALUE_COUNT);

ASSERT_NUM_STATE_BITS(kNumCompareOpBits, VK_COMPARE_OP_RANGE_SIZE);
ASSERT_NUM_STATE_BITS(kNumStencilOpBits, VK_STENCIL_OP_RANGE_SIZE);
ASSERT_NUM_STATE_BITS(kNumBlendFactorBits, VK_BLEND_FACTOR_RANGE_SIZE);
ASSERT_NUM_STATE_BITS(kNumBlendOpBits, VK_BLEND_OP_RANGE_SIZE);
ASSERT_NUM_STATE_BITS(kNumFrontFaceBits, VK_FRONT_FACE_RANGE_SIZE);
ASSERT_NUM_STATE_BITS(kNumTopologyBits, VK_PRIMITIVE_TOPOLOGY_RANGE_SIZE);

// Must adjust this in the unlikely case that more cull modes are added.
ASSERT_NUM_STATE_BITS(kNumCullModeBits,
                      VK_CULL_MODE_FRONT_AND_BACK - VK_CULL_MODE_NONE + 1);

#undef ASSERT_NUM_STATE_BITS

// Compilation should pass, but fail if you increase the padding by 1.
static_assert(sizeof(CommandBufferPipelineState::StaticState) == 16,
              "incorrect padding.");

CommandBufferPipelineState::CommandBufferPipelineState() = default;
CommandBufferPipelineState::~CommandBufferPipelineState() = default;

void CommandBufferPipelineState::BeginGraphicsOrComputeContext() {
  memset(vertex_bindings_.buffers, 0, sizeof(vertex_bindings_.buffers));
  dirty_vertex_bindings_ = ~0u;
}

vk::Pipeline CommandBufferPipelineState::FlushGraphicsPipeline(
    const PipelineLayout* pipeline_layout, ShaderProgram* program) {
  Hasher h;
  active_vertex_bindings_ = 0;
  uint32_t attribute_mask = pipeline_layout->spec().attribute_mask;
  ForEachBitIndex(attribute_mask, [&](uint32_t bit) {
    h.u32(bit);
    active_vertex_bindings_ |= 1u << vertex_attributes_[bit].binding;
    h.u32(vertex_attributes_[bit].binding);
    h.u32(static_cast<uint32_t>(vertex_attributes_[bit].format));
    h.u32(vertex_attributes_[bit].offset);
  });

  ForEachBitIndex(active_vertex_bindings_, [&](uint32_t bit) {
    h.u32(EnumCast(vertex_bindings_.input_rates[bit]));
    h.u32(vertex_bindings_.strides[bit]);
  });

  h.u64(render_pass_->uid());
  h.u32(current_subpass_);
  h.struc(static_state_);

  if (static_state_.blend_enable) {
    const auto needs_blend_constant = [](vk::BlendFactor factor) {
      return factor == vk::BlendFactor::eConstantColor ||
             factor == vk::BlendFactor::eConstantAlpha;
    };
    bool b0 = needs_blend_constant(static_state_.get_src_color_blend());
    bool b1 = needs_blend_constant(static_state_.get_src_alpha_blend());
    bool b2 = needs_blend_constant(static_state_.get_dst_color_blend());
    bool b3 = needs_blend_constant(static_state_.get_dst_alpha_blend());
    if (b0 || b1 || b2 || b3) {
      h.data(
          reinterpret_cast<uint32_t*>(potential_static_state_.blend_constants),
          sizeof(potential_static_state_.blend_constants));
    }
  }

  // Try to find a previously-stashed pipeline that matches the current command
  // state.  If none is found, build a new pipeline and stash it.
  Hash hash = h.value();
  if (auto pipeline = program->FindPipeline(hash)) {
    return pipeline;
  } else {
    pipeline = BuildGraphicsPipeline(pipeline_layout, program);
    program->StashPipeline(hash, pipeline);
    return pipeline;
  }
}

// Helper function for BuildGraphicsPipeline().
void CommandBufferPipelineState::InitPipelineColorBlendStateCreateInfo(
    vk::PipelineColorBlendStateCreateInfo* info,
    vk::PipelineColorBlendAttachmentState* blend_attachments,
    const impl::PipelineLayoutSpec& pipeline_layout_spec,
    const CommandBufferPipelineState::StaticState& static_state,
    const CommandBufferPipelineState::PotentialStaticState&
        potential_static_state,
    const impl::RenderPass* render_pass, uint32_t current_subpass) {
  info->pAttachments = blend_attachments;
  info->attachmentCount =
      render_pass->GetColorAttachmentCountForSubpass(current_subpass);
  for (unsigned i = 0; i < info->attachmentCount; i++) {
    auto& att = blend_attachments[i];
    auto& subpass_color_attachment =
        render_pass->GetColorAttachmentForSubpass(current_subpass, i);

    if (subpass_color_attachment.attachment != VK_ATTACHMENT_UNUSED &&
        (pipeline_layout_spec.render_target_mask & (1u << i))) {
      static_assert(VulkanLimits::kNumColorAttachments * 4 <=
                        sizeof(static_state.color_write_mask) * 8,
                    "not enough bits for color mask.");
      att.colorWriteMask = vk::ColorComponentFlags(
          (static_state.color_write_mask >> (4 * i)) & 0xf);
      att.blendEnable = static_state.blend_enable;
      if (att.blendEnable) {
        att.alphaBlendOp = vk::BlendOp(static_state.alpha_blend_op);
        att.colorBlendOp = vk::BlendOp(static_state.color_blend_op);
        att.dstAlphaBlendFactor = vk::BlendFactor(static_state.dst_alpha_blend);
        att.srcAlphaBlendFactor = vk::BlendFactor(static_state.src_alpha_blend);
        att.dstColorBlendFactor = vk::BlendFactor(static_state.dst_color_blend);
        att.srcColorBlendFactor = vk::BlendFactor(static_state.src_color_blend);
      }
    }
  }
  memcpy(info->blendConstants, potential_static_state.blend_constants,
         sizeof(info->blendConstants));
}

// Helper function for BuildGraphicsPipeline().
void CommandBufferPipelineState::InitPipelineDepthStencilStateCreateInfo(
    vk::PipelineDepthStencilStateCreateInfo* info,
    const CommandBufferPipelineState::StaticState& static_state, bool has_depth,
    bool has_stencil) {
  info->stencilTestEnable = has_stencil && static_state.stencil_test;
  info->depthTestEnable = has_depth && static_state.depth_test;
  info->depthWriteEnable = has_depth && static_state.depth_write;

  if (info->depthTestEnable) {
    info->depthCompareOp = vk::CompareOp(static_state.depth_compare);
  }

  if (info->stencilTestEnable) {
    info->front.compareOp =
        vk::CompareOp(static_state.stencil_front_compare_op);
    info->front.passOp = vk::StencilOp(static_state.stencil_front_pass);
    info->front.failOp = vk::StencilOp(static_state.stencil_front_fail);
    info->front.depthFailOp =
        vk::StencilOp(static_state.stencil_front_depth_fail);
    info->back.passOp = vk::StencilOp(static_state.stencil_back_pass);
    info->back.failOp = vk::StencilOp(static_state.stencil_back_fail);
    info->back.depthFailOp =
        vk::StencilOp(static_state.stencil_back_depth_fail);
  }
}

// Helper function for BuildGraphicsPipeline().
void CommandBufferPipelineState::InitPipelineVertexInputStateCreateInfo(
    vk::PipelineVertexInputStateCreateInfo* info,
    vk::VertexInputAttributeDescription* vertex_input_attribs,
    vk::VertexInputBindingDescription* vertex_input_bindings,
    uint32_t attr_mask,
    const CommandBufferPipelineState::VertexAttributeState* vertex_attributes,
    const CommandBufferPipelineState::VertexBindingState& vertex_bindings) {
  info->pVertexAttributeDescriptions = vertex_input_attribs;
  uint32_t binding_mask = 0;
  ForEachBitIndex(attr_mask, [&](uint32_t bit) {
    auto& attr = vertex_input_attribs[info->vertexAttributeDescriptionCount++];
    attr.location = bit;
    attr.binding = vertex_attributes[bit].binding;
    attr.format = vertex_attributes[bit].format;
    attr.offset = vertex_attributes[bit].offset;
    binding_mask |= 1u << attr.binding;
  });

  info->pVertexBindingDescriptions = vertex_input_bindings;
  ForEachBitIndex(binding_mask, [&](uint32_t bit) {
    auto& bind = vertex_input_bindings[info->vertexBindingDescriptionCount++];
    bind.binding = bit;
    bind.inputRate = vertex_bindings.input_rates[bit];
    bind.stride = vertex_bindings.strides[bit];
  });
}

// Helper function for BuildGraphicsPipeline().
void CommandBufferPipelineState::InitPipelineMultisampleStateCreateInfo(
    vk::PipelineMultisampleStateCreateInfo* info,
    const StaticState& static_state, vk::SampleCountFlagBits subpass_samples) {
  info->rasterizationSamples = subpass_samples;
  if (impl::SampleCountFlagBitsToInt(subpass_samples) > 1) {
    info->alphaToCoverageEnable = static_state.alpha_to_coverage;
    info->alphaToOneEnable = static_state.alpha_to_one;
    info->sampleShadingEnable = static_state.sample_shading;
    info->minSampleShading = 1.0f;
  }
}

// Helper function for BuildGraphicsPipeline().
void CommandBufferPipelineState::InitPipelineRasterizationStateCreateInfo(
    vk::PipelineRasterizationStateCreateInfo* info,
    const StaticState& static_state) {
  info->cullMode = vk::CullModeFlags(static_state.cull_mode);
  info->frontFace = vk::FrontFace(static_state.front_face);
  info->lineWidth = 1.0f;
  info->polygonMode =
      static_state.wireframe ? vk::PolygonMode::eLine : vk::PolygonMode::eFill;
  info->depthBiasEnable = static_state.depth_bias_enable != 0;
}

vk::Pipeline CommandBufferPipelineState::BuildGraphicsPipeline(
    const PipelineLayout* pipeline_layout, ShaderProgram* program) {
  TRACE_DURATION("gfx", "escher::CommandBuffer::BuildGraphicsPipeline");
  auto& pipeline_layout_spec = pipeline_layout->spec();

  // Viewport state
  vk::PipelineViewportStateCreateInfo viewport_info;
  viewport_info.viewportCount = 1;
  viewport_info.scissorCount = 1;

  // Dynamic state
  vk::PipelineDynamicStateCreateInfo dynamic_info;
  std::vector<vk::DynamicState> dynamic_states;
  dynamic_states.reserve(7);
  dynamic_states.push_back(vk::DynamicState::eScissor);
  dynamic_states.push_back(vk::DynamicState::eViewport);
  if (static_state_.depth_bias_enable) {
    dynamic_states.push_back(vk::DynamicState::eDepthBias);
  }
  if (static_state_.stencil_test) {
    dynamic_states.push_back(vk::DynamicState::eStencilCompareMask);
    dynamic_states.push_back(vk::DynamicState::eStencilReference);
    dynamic_states.push_back(vk::DynamicState::eStencilWriteMask);
  }
  dynamic_info.pDynamicStates = dynamic_states.data();
  dynamic_info.dynamicStateCount = dynamic_states.size();

  // Blend state
  vk::PipelineColorBlendStateCreateInfo blend_info;
  vk::PipelineColorBlendAttachmentState
      blend_attachments[VulkanLimits::kNumColorAttachments];
  InitPipelineColorBlendStateCreateInfo(
      &blend_info, blend_attachments, pipeline_layout_spec, static_state_,
      potential_static_state_, render_pass_, current_subpass_);

  // Depth state
  vk::PipelineDepthStencilStateCreateInfo depth_stencil_info;
  InitPipelineDepthStencilStateCreateInfo(
      &depth_stencil_info, static_state_,
      render_pass_->SubpassHasDepth(current_subpass_),
      render_pass_->SubpassHasStencil(current_subpass_));

  // Vertex input
  vk::PipelineVertexInputStateCreateInfo vertex_input_info;
  vk::VertexInputAttributeDescription
      vertex_input_attribs[VulkanLimits::kNumVertexAttributes];
  vk::VertexInputBindingDescription
      vertex_input_bindings[VulkanLimits::kNumVertexBuffers];
  InitPipelineVertexInputStateCreateInfo(
      &vertex_input_info, vertex_input_attribs, vertex_input_bindings,
      pipeline_layout_spec.attribute_mask, vertex_attributes_,
      vertex_bindings_);

  // Input assembly
  vk::PipelineInputAssemblyStateCreateInfo assembly_info;
  assembly_info.primitiveRestartEnable = static_state_.primitive_restart;
  assembly_info.topology = static_state_.get_primitive_topology();

  // Multisample
  vk::PipelineMultisampleStateCreateInfo multisample_info;
  InitPipelineMultisampleStateCreateInfo(
      &multisample_info, static_state_,
      render_pass_->SubpassSamples(current_subpass_));

  // Rasterization
  vk::PipelineRasterizationStateCreateInfo rasterization_info;
  InitPipelineRasterizationStateCreateInfo(&rasterization_info, static_state_);

  // Pipeline Stages
  vk::PipelineShaderStageCreateInfo stages[EnumCount<ShaderStage>()];
  unsigned num_stages = 0;

  for (size_t i = 0; i < EnumCount<ShaderStage>(); ++i) {
    auto& module = program->GetModuleForStage(static_cast<ShaderStage>(i));
    if (module) {
      auto& s = stages[num_stages++];
      s.module = module->vk();
      s.pName = "main";
      s.stage = ShaderStageToFlags(module->shader_stage());
    }
  }

  vk::GraphicsPipelineCreateInfo pipeline_info;
  pipeline_info.layout = pipeline_layout->vk();
  pipeline_info.renderPass = render_pass_->vk();
  pipeline_info.subpass = current_subpass_;

  pipeline_info.pViewportState = &viewport_info;
  pipeline_info.pDynamicState = &dynamic_info;
  pipeline_info.pColorBlendState = &blend_info;
  pipeline_info.pDepthStencilState = &depth_stencil_info;
  pipeline_info.pVertexInputState = &vertex_input_info;
  pipeline_info.pInputAssemblyState = &assembly_info;
  pipeline_info.pMultisampleState = &multisample_info;
  pipeline_info.pRasterizationState = &rasterization_info;
  pipeline_info.pStages = stages;
  pipeline_info.stageCount = num_stages;

  TRACE_DURATION("gfx", "escher::CommandBuffer::BuildGraphicsPipeline[vulkan]");
  return ESCHER_CHECKED_VK_RESULT(
      program->vk_device().createGraphicsPipeline(nullptr, pipeline_info));
}

void CommandBufferPipelineState::SetVertexAttributes(uint32_t binding,
                                                     uint32_t attrib,
                                                     vk::Format format,
                                                     vk::DeviceSize offset) {
  FXL_DCHECK(binding < VulkanLimits::kNumVertexBuffers);
  FXL_DCHECK(attrib < VulkanLimits::kNumVertexAttributes);

  auto& attr = vertex_attributes_[attrib];
  if (attr.binding != binding || attr.format != format ||
      attr.offset != offset) {
    attr.binding = binding;
    attr.format = format;
    attr.offset = offset;
  }
}

bool CommandBufferPipelineState::BindVertices(uint32_t binding,
                                              const BufferPtr& buffer,
                                              vk::DeviceSize offset,
                                              vk::DeviceSize stride,
                                              vk::VertexInputRate step_rate) {
  FXL_DCHECK(binding < VulkanLimits::kNumVertexBuffers);

  auto vk_buffer = buffer->vk();
  if (vertex_bindings_.buffers[binding] != vk_buffer ||
      vertex_bindings_.offsets[binding] != offset) {
    dirty_vertex_bindings_ |= 1u << binding;
  }

  vertex_bindings_.buffers[binding] = vk_buffer;
  vertex_bindings_.offsets[binding] = offset;
  vertex_bindings_.strides[binding] = stride;
  vertex_bindings_.input_rates[binding] = step_rate;

  // Pipeline change is required if either stride or input-rate changes.
  return vertex_bindings_.strides[binding] != stride ||
         vertex_bindings_.input_rates[binding] != step_rate;
}

void CommandBufferPipelineState::FlushVertexBuffers(vk::CommandBuffer cb) {
  uint32_t update_vbo_mask = dirty_vertex_bindings_ & active_vertex_bindings_;
  ForEachBitRange(
      update_vbo_mask, [&](uint32_t binding, uint32_t binding_count) {
#ifndef NDEBUG
        for (unsigned i = binding; i < binding + binding_count; i++) {
          FXL_DCHECK(vertex_bindings_.buffers[i]);
        }
#endif
        cb.bindVertexBuffers(binding, binding_count,
                             vertex_bindings_.buffers + binding,
                             vertex_bindings_.offsets + binding);
      });
  dirty_vertex_bindings_ &= ~update_vbo_mask;
}

}  // namespace escher
