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

#ifndef SRC_UI_LIB_ESCHER_THIRD_PARTY_GRANITE_VK_COMMAND_BUFFER_PIPELINE_STATE_H_
#define SRC_UI_LIB_ESCHER_THIRD_PARTY_GRANITE_VK_COMMAND_BUFFER_PIPELINE_STATE_H_

#include <cstdint>

#include "src/lib/fxl/memory/ref_ptr.h"
#include "src/ui/lib/escher/forward_declarations.h"
#include "src/ui/lib/escher/third_party/granite/vk/pipeline_layout.h"
#include "src/ui/lib/escher/util/enum_cast.h"
#include "src/ui/lib/escher/util/hash.h"
#include "src/ui/lib/escher/vk/vulkan_limits.h"

#include <vulkan/vulkan.hpp>

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
  explicit CommandBufferPipelineState(fxl::WeakPtr<PipelineBuilder> pipeline_builder);
  ~CommandBufferPipelineState();

  void BeginGraphicsOrComputeContext();

  // Use |layout| and |program| to compute a hash that is used to look up the corresponding
  // vk::Pipeline.  If no pipeline is found, then if |allow_build_pipeline| == true, a new pipeline
  // is lazily generated and cached for next time; otherwise a CHECK fails.
  vk::Pipeline FlushGraphicsPipeline(const PipelineLayout* layout, ShaderProgram* program,
                                     bool log_pipeline_creation = false);
  vk::Pipeline FlushComputePipeline(const PipelineLayout* layout, ShaderProgram* program,
                                    bool log_pipeline_creation = false);

  // Helper function used by |FlushGraphicsPipeline()|, and by tests.  Generates a new vk::Pipeline.
  vk::Pipeline BuildGraphicsPipeline(const PipelineLayout* layout, ShaderProgram* program,
                                     bool log_pipeline_creation);

  // Helper function used by |BuildGraphicsPipeline()|, and by tests.  Uses |allocator| to allocate
  // a new vk::GraphicsPipelineCreateInfo, as well as other Vulkan structs pointed by it.
  vk::GraphicsPipelineCreateInfo* InitGraphicsPipelineCreateInfo(BlockAllocator* allocator,
                                                                 const PipelineLayout* layout,
                                                                 ShaderProgram* program);

  struct StaticState;
  StaticState* static_state() { return &static_state_; }
  const StaticState* static_state() const { return &static_state_; }

  struct PotentialStaticState;
  PotentialStaticState* potential_static_state() { return &potential_static_state_; }
  const PotentialStaticState* potential_static_state() const { return &potential_static_state_; }

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

  // Convenient way to bring CommandBuffer to a known default state.  See the
  // implementation of SetToDefaultState() for more details; it's basically a
  // big switch statement.
  enum class DefaultState {
    kOpaque,
    // TODO(47918): Add command buffer state for non-premultiplied alpha.
    //
    // The intuition is more clearly expressed in terms of "transparency"
    // instead of "alpha", where the former is defined as 1-alpha.
    // If the transparencies of the fragment and destination pixel are,
    // respectively:
    //   X' == 1-X
    //   Y' == 1-Y
    // ... then we want the blended output to have transparency (X' * Y').
    // In terms of alpha, this is:
    //   1 - ((1-X) * (1-Y))  ==
    //   1 - (1 - X - Y + XY) ==
    //   X + Y - XY           ==
    //   X + Y * (1-X)
    //
    // Here we assume that all colors are premultiplied alpha, so
    // the blended output should be
    //   RGB = RGB(src) + (1 - A(src)) RGB(dst)
    //     A =   A(src) + (1 - A(src)) A(dst)
    //
    // We express this with the following blend-factors:
    //   src_color_blend == src_alpha_blend == ONE
    //   dst_color_blend == dst_alpha_blend == ONE_MINUS_SRC_ALPHA
    kTranslucent,
    kWireframe
  };
  void SetToDefaultState(DefaultState state);

  impl::RenderPass* render_pass() { return render_pass_; }
  void set_render_pass(impl::RenderPass* render_pass) {
    // Can only set to non-null if currently null, and vice-versa.
    FX_DCHECK(!render_pass_ == !!render_pass);
    render_pass_ = render_pass;
  }

  // Static state setters; these match the setters on CommandBuffer.
  void SetDepthTestAndWrite(bool depth_test, bool depth_write);
  void SetWireframe(bool wireframe);
  void SetDepthCompareOp(vk::CompareOp depth_compare);
  void SetBlendEnable(bool blend_enable);
  void SetBlendFactors(vk::BlendFactor src_color_blend, vk::BlendFactor src_alpha_blend,
                       vk::BlendFactor dst_color_blend, vk::BlendFactor dst_alpha_blend);
  void SetBlendFactors(vk::BlendFactor src_blend, vk::BlendFactor dst_blend);
  void SetBlendOp(vk::BlendOp color_blend_op, vk::BlendOp alpha_blend_op);
  void SetBlendOp(vk::BlendOp blend_op);
  void SetColorWriteMask(uint32_t color_write_mask);
  void SetDepthBias(bool depth_bias_enable);
  void SetStencilTest(bool stencil_test);
  void SetStencilFrontOps(vk::CompareOp stencil_front_compare_op, vk::StencilOp stencil_front_pass,
                          vk::StencilOp stencil_front_fail, vk::StencilOp stencil_front_depth_fail);
  void SetStencilBackOps(vk::CompareOp stencil_back_compare_op, vk::StencilOp stencil_back_pass,
                         vk::StencilOp stencil_back_fail, vk::StencilOp stencil_back_depth_fail);
  void SetStencilOps(vk::CompareOp stencil_compare_op, vk::StencilOp stencil_pass,
                     vk::StencilOp stencil_fail, vk::StencilOp stencil_depth_fail);
  void SetPrimitiveTopology(vk::PrimitiveTopology primitive_topology);
  void SetPrimitiveRestart(bool primitive_restart);
  void SetMultisampleState(bool alpha_to_coverage, bool alpha_to_one, bool sample_shading);
  void SetFrontFace(vk::FrontFace front_face);
  void SetCullMode(vk::CullModeFlags cull_mode);

  struct StaticState {
    static constexpr uint8_t kNumBooleanBits = 1;
    static constexpr uint8_t kNumCompareOpBits = 3;
    static constexpr uint8_t kNumStencilOpBits = 3;
    static constexpr uint8_t kNumBlendFactorBits = 5;
    static constexpr uint8_t kNumBlendOpBits = 3;
    static constexpr uint8_t kNumCullModeBits = 2;
    static constexpr uint8_t kNumFrontFaceBits = 1;
    static constexpr uint8_t kNumPrimitiveTopologyBits = 4;

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
    unsigned primitive_topology : kNumPrimitiveTopologyBits;

    unsigned wireframe : kNumBooleanBits;  // TODO: support all vk::PolygonMode

    // The bits above total is < 3 * sizeof(uint32_t).  Pad to multiple of sizeof(uint32_t).
    const unsigned padding : 26;

    uint32_t color_write_mask;

    bool get_depth_write() const { return static_cast<bool>(depth_write); }
    bool get_depth_test() const { return static_cast<bool>(depth_test); }
    bool get_blend_enable() const { return static_cast<bool>(blend_enable); }
    vk::CullModeFlags get_cull_mode() const { return static_cast<vk::CullModeFlags>(cull_mode); }
    vk::FrontFace get_front_face() const { return static_cast<vk::FrontFace>(front_face); }
    bool get_depth_bias_enable() const { return static_cast<bool>(depth_bias_enable); }
    vk::CompareOp get_depth_compare() const { return static_cast<vk::CompareOp>(depth_compare); }
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

    bool get_alpha_to_coverage() const { return static_cast<bool>(alpha_to_coverage); }
    bool get_alpha_to_one() const { return static_cast<bool>(alpha_to_one); }
    bool get_sample_shading() const { return static_cast<bool>(sample_shading); }

    vk::BlendFactor get_src_color_blend() const {
      return static_cast<vk::BlendFactor>(src_color_blend);
    }
    vk::BlendFactor get_dst_color_blend() const {
      return static_cast<vk::BlendFactor>(dst_color_blend);
    }
    vk::BlendOp get_color_blend_op() const { return static_cast<vk::BlendOp>(color_blend_op); }
    vk::BlendFactor get_src_alpha_blend() const {
      return static_cast<vk::BlendFactor>(src_alpha_blend);
    }
    vk::BlendFactor get_dst_alpha_blend() const {
      return static_cast<vk::BlendFactor>(dst_alpha_blend);
    }
    vk::BlendOp get_alpha_blend_op() const { return static_cast<vk::BlendOp>(alpha_blend_op); }
    bool get_primitive_restart() const { return static_cast<bool>(primitive_restart); }
    vk::PrimitiveTopology get_primitive_topology() const {
      return static_cast<vk::PrimitiveTopology>(primitive_topology);
    }

    bool get_wireframe() const { return static_cast<bool>(wireframe); }

    uint32_t get_color_write_mask() const { return color_write_mask; };

    bool operator==(const StaticState& state) const {
      return 0 == memcmp(this, &state, sizeof(StaticState));
    }
  };

  struct PotentialStaticState {
    float blend_constants[4];
  };

  // Helper for unpacking Vulkan enums into an unsigned which can be stored in a StaticState field,
  // ensuring that it will fit in the alloted number of bits is not exceeded.  For example,
  // VK_BLEND_OP_HARDLIGHT_EXT is 1000148019, which will not fit in kNumBlendOpBits == 3.  If
  // such values become necessary in the future; this design will need to be revisited.
  static unsigned UnpackEnum(vk::CompareOp val);
  static unsigned UnpackEnum(vk::StencilOp val);
  static unsigned UnpackEnum(vk::BlendFactor val);
  static unsigned UnpackEnum(vk::BlendOp val);
  static unsigned UnpackEnum(vk::FrontFace val);
  static unsigned UnpackEnum(vk::PrimitiveTopology val);
  // These are not enums, but this name allows CommandBuffer's SET_STATIC_STATE_ENUM macro to work.
  static unsigned UnpackEnum(vk::CullModeFlags val);

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

  vk::Pipeline BuildComputePipeline(const PipelineLayout* layout, ShaderProgram* program,
                                    bool log_pipeline_creation);

  // Helper functions for BuildGraphicsPipeline().
  static void InitPipelineColorBlendStateCreateInfo(
      vk::PipelineColorBlendStateCreateInfo* info,
      vk::PipelineColorBlendAttachmentState* blend_attachments,
      const impl::PipelineLayoutSpec& pipeline_layout_spec, const StaticState& static_state,
      const PotentialStaticState& potential_static_state, const impl::RenderPass* render_pass,
      uint32_t current_subpass);
  static void InitPipelineDepthStencilStateCreateInfo(vk::PipelineDepthStencilStateCreateInfo* info,
                                                      const StaticState& static_state,
                                                      bool has_depth, bool has_stencil);
  static void InitPipelineVertexInputStateCreateInfo(
      vk::PipelineVertexInputStateCreateInfo* info,
      vk::VertexInputAttributeDescription* vertex_input_attribs,
      vk::VertexInputBindingDescription* vertex_input_bindings, uint32_t attr_mask,
      const VertexAttributeState* vertex_attributes, const VertexBindingState& vertex_bindings);
  static void InitPipelineMultisampleStateCreateInfo(vk::PipelineMultisampleStateCreateInfo* info,
                                                     const StaticState& static_state,
                                                     vk::SampleCountFlagBits subpass_samples);
  static void InitPipelineRasterizationStateCreateInfo(
      vk::PipelineRasterizationStateCreateInfo* info, const StaticState& static_state);

  fxl::WeakPtr<PipelineBuilder> pipeline_builder_;

  impl::RenderPass* render_pass_ = nullptr;

  // TODO(fxbug.dev/7174): need support for updating current subpass.
  uint32_t current_subpass_ = 0;

  StaticState static_state_ = {};
  PotentialStaticState potential_static_state_ = {};
  VertexAttributeState vertex_attributes_[VulkanLimits::kNumVertexAttributes] = {};
  VertexBindingState vertex_bindings_ = {};
  uint32_t active_vertex_bindings_ = 0;
  uint32_t dirty_vertex_bindings_ = 0;
};

ESCHER_DEBUG_PRINTABLE(CommandBufferPipelineState::StaticState);

// Inline function definitions - static state setters.

// Macro to avoid typing boilerplate UnpackEnum() implementations.
#define UNPACK_ENUM_IMPL(TYPE)                                           \
  inline unsigned CommandBufferPipelineState::UnpackEnum(vk::TYPE val) { \
    FX_DCHECK(0 == (EnumCast(val) >> StaticState::kNum##TYPE##Bits))     \
        << "enum does not fit: " << vk::to_string(val);                  \
    return EnumCast(val);                                                \
  }

// Macro to avoid typing boilerplate UnpackEnum() implementations.
#define UNPACK_FLAGS_ENUM_IMPL(TYPE)                                            \
  inline unsigned CommandBufferPipelineState::UnpackEnum(vk::TYPE##Flags val) { \
    unsigned result = static_cast<Vk##CullMode##Flags>(val);                    \
    FX_DCHECK(0 == (result >> StaticState::kNum##TYPE##Bits))                   \
        << "enum does not fit: " << vk::to_string(val);                         \
    return result;                                                              \
  }

UNPACK_ENUM_IMPL(CompareOp);
UNPACK_ENUM_IMPL(StencilOp);
UNPACK_ENUM_IMPL(BlendFactor);
UNPACK_ENUM_IMPL(BlendOp);
UNPACK_ENUM_IMPL(FrontFace);
UNPACK_ENUM_IMPL(PrimitiveTopology);
UNPACK_FLAGS_ENUM_IMPL(CullMode);

#undef UNPACK_ENUM_IMPL
#undef UNPACK_FLAGS_ENUM_IMPL

inline void CommandBufferPipelineState::SetDepthTestAndWrite(bool depth_test, bool depth_write) {
  static_state_.depth_test = depth_test;
  static_state_.depth_write = depth_write;
}

inline void CommandBufferPipelineState::SetWireframe(bool wireframe) {
  static_state_.wireframe = wireframe;
}

inline void CommandBufferPipelineState::SetDepthCompareOp(vk::CompareOp depth_compare) {
  static_state_.depth_compare = UnpackEnum(depth_compare);
}

inline void CommandBufferPipelineState::SetBlendEnable(bool blend_enable) {
  static_state_.blend_enable = blend_enable;
}

inline void CommandBufferPipelineState::SetBlendFactors(vk::BlendFactor src_color_blend,
                                                        vk::BlendFactor src_alpha_blend,
                                                        vk::BlendFactor dst_color_blend,
                                                        vk::BlendFactor dst_alpha_blend) {
  static_state_.src_color_blend = UnpackEnum(src_color_blend);
  static_state_.src_alpha_blend = UnpackEnum(src_alpha_blend);
  static_state_.dst_color_blend = UnpackEnum(dst_color_blend);
  static_state_.dst_alpha_blend = UnpackEnum(dst_alpha_blend);
}

inline void CommandBufferPipelineState::SetBlendFactors(vk::BlendFactor src_blend,
                                                        vk::BlendFactor dst_blend) {
  SetBlendFactors(src_blend, src_blend, dst_blend, dst_blend);
}

inline void CommandBufferPipelineState::SetBlendOp(vk::BlendOp color_blend_op,
                                                   vk::BlendOp alpha_blend_op) {
  static_state_.color_blend_op = UnpackEnum(color_blend_op);
  static_state_.alpha_blend_op = UnpackEnum(alpha_blend_op);
}

inline void CommandBufferPipelineState::SetBlendOp(vk::BlendOp blend_op) {
  SetBlendOp(blend_op, blend_op);
}

inline void CommandBufferPipelineState::SetColorWriteMask(uint32_t color_write_mask) {
  static_state_.color_write_mask = color_write_mask;
}

inline void CommandBufferPipelineState::SetDepthBias(bool depth_bias_enable) {
  static_state_.depth_bias_enable = depth_bias_enable;
}

inline void CommandBufferPipelineState::SetStencilTest(bool stencil_test) {
  static_state_.stencil_test = stencil_test;
}

inline void CommandBufferPipelineState::SetStencilFrontOps(vk::CompareOp stencil_front_compare_op,
                                                           vk::StencilOp stencil_front_pass,
                                                           vk::StencilOp stencil_front_fail,
                                                           vk::StencilOp stencil_front_depth_fail) {
  static_state_.stencil_front_compare_op = UnpackEnum(stencil_front_compare_op);
  static_state_.stencil_front_pass = UnpackEnum(stencil_front_pass);
  static_state_.stencil_front_fail = UnpackEnum(stencil_front_fail);
  static_state_.stencil_front_depth_fail = UnpackEnum(stencil_front_depth_fail);
}

inline void CommandBufferPipelineState::SetStencilBackOps(vk::CompareOp stencil_back_compare_op,
                                                          vk::StencilOp stencil_back_pass,
                                                          vk::StencilOp stencil_back_fail,
                                                          vk::StencilOp stencil_back_depth_fail) {
  static_state_.stencil_back_compare_op = UnpackEnum(stencil_back_compare_op);
  static_state_.stencil_back_pass = UnpackEnum(stencil_back_pass);
  static_state_.stencil_back_fail = UnpackEnum(stencil_back_fail);
  static_state_.stencil_back_depth_fail = UnpackEnum(stencil_back_depth_fail);
}

inline void CommandBufferPipelineState::SetStencilOps(vk::CompareOp stencil_compare_op,
                                                      vk::StencilOp stencil_pass,
                                                      vk::StencilOp stencil_fail,
                                                      vk::StencilOp stencil_depth_fail) {
  SetStencilFrontOps(stencil_compare_op, stencil_pass, stencil_fail, stencil_depth_fail);
  SetStencilBackOps(stencil_compare_op, stencil_pass, stencil_fail, stencil_depth_fail);
}

inline void CommandBufferPipelineState::SetPrimitiveTopology(
    vk::PrimitiveTopology primitive_topology) {
  static_state_.primitive_topology = UnpackEnum(primitive_topology);
}

inline void CommandBufferPipelineState::SetPrimitiveRestart(bool primitive_restart) {
  static_state_.primitive_restart = primitive_restart;
}

inline void CommandBufferPipelineState::SetMultisampleState(bool alpha_to_coverage,
                                                            bool alpha_to_one,
                                                            bool sample_shading) {
  static_state_.alpha_to_coverage = alpha_to_coverage;
  static_state_.alpha_to_one = alpha_to_one;
  static_state_.sample_shading = sample_shading;
}

inline void CommandBufferPipelineState::SetFrontFace(vk::FrontFace front_face) {
  static_state_.front_face = UnpackEnum(front_face);
}

inline void CommandBufferPipelineState::SetCullMode(vk::CullModeFlags cull_mode) {
  static_state_.cull_mode = UnpackEnum(cull_mode);
}

#undef SET_STATIC_STATE_ENUM

}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_THIRD_PARTY_GRANITE_VK_COMMAND_BUFFER_PIPELINE_STATE_H_
