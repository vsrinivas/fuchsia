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
// - vulkan/command_buffer.hpp

#ifndef LIB_ESCHER_THIRD_PARTY_GRANITE_VK_COMMAND_BUFFER_PIPELINE_STATE_H_
#define LIB_ESCHER_THIRD_PARTY_GRANITE_VK_COMMAND_BUFFER_PIPELINE_STATE_H_

#include <cstdint>
#include <vulkan/vulkan.hpp>

#include "lib/escher/forward_declarations.h"
#include "lib/escher/third_party/granite/vk/pipeline_layout.h"
#include "lib/escher/util/hash.h"
#include "lib/escher/vk/vulkan_limits.h"
#include "lib/fxl/memory/ref_ptr.h"

namespace escher {

class PipelineLayout;
using PipelineLayoutPtr = fxl::RefPtr<PipelineLayout>;
class ShaderProgram;
using ShaderProgramPtr = fxl::RefPtr<ShaderProgram>;

// CommandBufferPipelineState is a helper class used by CommandBuffer to
// encapsulate the subset of state that, when changed, requires a corresponding
// change to the VkPipeline that is used.
class CommandBufferPipelineState {
 public:
  CommandBufferPipelineState();
  ~CommandBufferPipelineState();

  void BeginGraphicsOrComputeContext();

  vk::Pipeline FlushGraphicsPipeline(const PipelineLayout* layout,
                                     ShaderProgram* program);

  struct StaticState;
  StaticState* static_state() { return &static_state_; }
  const StaticState* static_state() const { return &static_state_; }

  struct PotentialStaticState;
  PotentialStaticState* potential_static_state() {
    return &potential_static_state_;
  }
  const PotentialStaticState* potential_static_state() const {
    return &potential_static_state_;
  }

  // Called by CommandBuffer::SetVertexAttributes().
  void SetVertexAttributes(uint32_t binding, uint32_t attrib, vk::Format format,
                           vk::DeviceSize offset);

  // Called by CommandBuffer::BindVertices().  Return true if pipeline change is
  // required.
  bool BindVertices(uint32_t binding, vk::Buffer buffer, vk::DeviceSize offset,
                    vk::DeviceSize stride, vk::VertexInputRate step_rate);

  // Called by CommandBuffer::FlushRenderState().  Binds any vertex buffers that
  // are both dirty and active in the current pipeline layout.
  void FlushVertexBuffers(vk::CommandBuffer cb);

  impl::RenderPass* render_pass() { return render_pass_; }
  void set_render_pass(impl::RenderPass* render_pass) {
    // Can only set to non-null if currently null, and vice-versa.
    FXL_DCHECK(!render_pass_ == !!render_pass);
    render_pass_ = render_pass;
  }

  struct StaticState {
    static constexpr uint8_t kNumBooleanBits = 1;
    static constexpr uint8_t kNumCompareOpBits = 3;
    static constexpr uint8_t kNumStencilOpBits = 3;
    static constexpr uint8_t kNumBlendFactorBits = 5;
    static constexpr uint8_t kNumBlendOpBits = 3;
    static constexpr uint8_t kNumCullModeBits = 2;
    static constexpr uint8_t kNumFrontFaceBits = 1;
    static constexpr uint8_t kNumTopologyBits = 4;

    unsigned depth_write : kNumBooleanBits;
    unsigned depth_test : kNumBooleanBits;
    unsigned blend_enable : kNumBooleanBits;

    unsigned cull_mode : kNumCullModeBits;
    unsigned front_face : kNumFrontFaceBits;
    unsigned depth_bias_enable : kNumBooleanBits;

    unsigned depth_compare : kNumCompareOpBits;

    unsigned stencil_test : kNumBooleanBits;
    unsigned stencil_front_fail : kNumStencilOpBits;
    unsigned stencil_front_pass : kNumStencilOpBits;
    unsigned stencil_front_depth_fail : kNumStencilOpBits;
    unsigned stencil_front_compare_op : kNumCompareOpBits;
    unsigned stencil_back_fail : kNumStencilOpBits;
    unsigned stencil_back_pass : kNumStencilOpBits;
    unsigned stencil_back_depth_fail : kNumStencilOpBits;
    unsigned stencil_back_compare_op : kNumCompareOpBits;

    unsigned alpha_to_coverage : kNumBooleanBits;
    unsigned alpha_to_one : kNumBooleanBits;
    unsigned sample_shading : kNumBooleanBits;

    unsigned src_color_blend : kNumBlendFactorBits;
    unsigned dst_color_blend : kNumBlendFactorBits;
    unsigned color_blend_op : kNumBlendOpBits;
    unsigned src_alpha_blend : kNumBlendFactorBits;
    unsigned dst_alpha_blend : kNumBlendFactorBits;
    unsigned alpha_blend_op : kNumBlendOpBits;
    unsigned primitive_restart : kNumBooleanBits;
    unsigned primitive_topology : kNumTopologyBits;

    unsigned wireframe : kNumBooleanBits;  // TODO: support all vk::PolygonMode

    // Pad previous bits to 4 * sizeof(uint32_t).
    unsigned padding : 26;

    uint32_t color_write_mask;

    bool get_depth_write() const { return static_cast<bool>(depth_write); }
    bool get_depth_test() const { return static_cast<bool>(depth_test); }
    bool get_blend_enable() const { return static_cast<bool>(blend_enable); }
    vk::CullModeFlags get_cull_mode() const {
      return static_cast<vk::CullModeFlags>(cull_mode);
    }
    vk::FrontFace get_front_face() const {
      return static_cast<vk::FrontFace>(front_face);
    }
    bool get_depth_bias_enable() const {
      return static_cast<bool>(depth_bias_enable);
    }
    vk::CompareOp get_depth_compare() const {
      return static_cast<vk::CompareOp>(depth_compare);
    }
    bool get_stencil_test() const { return static_cast<bool>(stencil_test); }
    vk::StencilOp get_stencil_front_fail() const {
      return static_cast<vk::StencilOp>(stencil_front_fail);
    }
    vk::StencilOp get_stencil_front_pass() const {
      return static_cast<vk::StencilOp>(stencil_front_pass);
    }
    vk::StencilOp get_stencil_front_depth_fail() const {
      return static_cast<vk::StencilOp>(stencil_front_depth_fail);
    }
    vk::CompareOp get_stencil_front_compare_op() const {
      return static_cast<vk::CompareOp>(stencil_front_compare_op);
    }
    vk::StencilOp get_stencil_back_fail() const {
      return static_cast<vk::StencilOp>(stencil_back_fail);
    }
    vk::StencilOp get_stencil_back_pass() const {
      return static_cast<vk::StencilOp>(stencil_back_pass);
    }
    vk::StencilOp get_stencil_back_depth_fail() const {
      return static_cast<vk::StencilOp>(stencil_back_depth_fail);
    }
    vk::CompareOp get_stencil_back_compare_op() const {
      return static_cast<vk::CompareOp>(stencil_back_compare_op);
    }

    bool get_alpha_to_coverage() const {
      return static_cast<bool>(alpha_to_coverage);
    }
    bool get_alpha_to_one() const { return static_cast<bool>(alpha_to_one); }
    bool get_sample_shading() const {
      return static_cast<bool>(sample_shading);
    }

    vk::BlendFactor get_src_color_blend() const {
      return static_cast<vk::BlendFactor>(src_color_blend);
    }
    vk::BlendFactor get_dst_color_blend() const {
      return static_cast<vk::BlendFactor>(dst_color_blend);
    }
    vk::BlendOp get_color_blend_op() const {
      return static_cast<vk::BlendOp>(color_blend_op);
    }
    vk::BlendFactor get_src_alpha_blend() const {
      return static_cast<vk::BlendFactor>(src_alpha_blend);
    }
    vk::BlendFactor get_dst_alpha_blend() const {
      return static_cast<vk::BlendFactor>(dst_alpha_blend);
    }
    vk::BlendOp get_alpha_blend_op() const {
      return static_cast<vk::BlendOp>(alpha_blend_op);
    }
    bool get_primitive_restart() const {
      return static_cast<bool>(primitive_restart);
    }
    vk::PrimitiveTopology get_primitive_topology() const {
      return static_cast<vk::PrimitiveTopology>(primitive_topology);
    }

    bool get_wireframe() const { return static_cast<bool>(wireframe); }
  };

  struct PotentialStaticState {
    float blend_constants[4];
  };

 private:
  friend class VulkanTester;

  struct VertexAttributeState {
    uint32_t binding;
    vk::Format format;
    uint32_t offset;
  };

  struct VertexBindingState {
    vk::Buffer buffers[VulkanLimits::kNumVertexBuffers];
    vk::DeviceSize offsets[VulkanLimits::kNumVertexBuffers];
    vk::DeviceSize strides[VulkanLimits::kNumVertexBuffers];
    vk::VertexInputRate input_rates[VulkanLimits::kNumVertexBuffers];
  };

  vk::Pipeline BuildGraphicsPipeline(const PipelineLayout* layout,
                                     ShaderProgram* program);

  // Helper functions for BuildGraphicsPipeline().
  static void InitPipelineColorBlendStateCreateInfo(
      vk::PipelineColorBlendStateCreateInfo* info,
      vk::PipelineColorBlendAttachmentState* blend_attachments,
      const impl::PipelineLayoutSpec& pipeline_layout_spec,
      const StaticState& static_state,
      const PotentialStaticState& potential_static_state,
      const impl::RenderPass* render_pass, uint32_t current_subpass);
  static void InitPipelineDepthStencilStateCreateInfo(
      vk::PipelineDepthStencilStateCreateInfo* info,
      const StaticState& static_state, bool has_depth, bool has_stencil);
  static void InitPipelineVertexInputStateCreateInfo(
      vk::PipelineVertexInputStateCreateInfo* info,
      vk::VertexInputAttributeDescription* vertex_input_attribs,
      vk::VertexInputBindingDescription* vertex_input_bindings,
      uint32_t attr_mask, const VertexAttributeState* vertex_attributes,
      const VertexBindingState& vertex_bindings);
  static void InitPipelineMultisampleStateCreateInfo(
      vk::PipelineMultisampleStateCreateInfo* info,
      const StaticState& static_state, vk::SampleCountFlagBits subpass_samples);
  static void InitPipelineRasterizationStateCreateInfo(
      vk::PipelineRasterizationStateCreateInfo* info,
      const StaticState& static_state);

  impl::RenderPass* render_pass_ = nullptr;

  // TODO(ES-83): need support for updating current subpass.
  uint32_t current_subpass_ = 0;

  StaticState static_state_ = {};
  PotentialStaticState potential_static_state_ = {};
  VertexAttributeState vertex_attributes_[VulkanLimits::kNumVertexAttributes] =
      {};
  VertexBindingState vertex_bindings_ = {};
  uint32_t active_vertex_bindings_ = 0;
  uint32_t dirty_vertex_bindings_ = 0;
};

}  // namespace escher

#endif  // LIB_ESCHER_THIRD_PARTY_GRANITE_VK_COMMAND_BUFFER_PIPELINE_STATE_H_
