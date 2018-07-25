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

#ifndef LIB_ESCHER_THIRD_PARTY_GRANITE_VK_COMMAND_BUFFER_H_
#define LIB_ESCHER_THIRD_PARTY_GRANITE_VK_COMMAND_BUFFER_H_

#include <vulkan/vulkan.hpp>

#include "lib/escher/base/reffable.h"
#include "lib/escher/third_party/granite/vk/command_buffer_pipeline_state.h"
#include "lib/escher/third_party/granite/vk/pipeline_layout.h"
#include "lib/escher/util/enum_cast.h"
#include "lib/escher/vk/render_pass_info.h"

// TODO(ES-83): CommandBuffer currently wraps an old-style impl::CommandBuffer.
#include "lib/escher/impl/command_buffer.h"

namespace escher {

class Escher;

class CommandBuffer;
using CommandBufferPtr = fxl::RefPtr<CommandBuffer>;

// CommandBuffer is a wrapper around VkCommandBuffer, which significantly
// improves upon the usability of the raw Vulkan object in a number of ways.
//
// Most notably, CommandBuffer provides an "OpenGL-like" approach to resource
// binding and pipeline generation.  Users of CommandBuffer never directly deal
// with VkPipelines, VkRenderPasses, VkFramebuffers, and others; these are
// created behind the scenes, and cached for efficiency.  For example, the exact
// same shader code might require muliple VkPipeline variants to be generated,
// for example if different depth-comparison ops are to be used.  CommandBuffer
// frees clients of the burden of manually generating and managing these
// VkPipeline variants.  Instead, clients simply call SetShaderProgram(), and
// the appropriate variants are lazily generated when necessary, based on the
// ShaderProgram and other CommandBuffer state (e.g. depth/stencil state, the
// strides/formats/offsets of currently-bound vertex buffers, etc.).
//
// Another major convenience provided by CommandBuffer is life-cycle management
// of resources that are no longer needed.  Vulkan forbids client applications
// from destroying any resource that is referenced by a "pending command buffer"
// (i.e. one whose commands have not finished executing on the GPU).  Instead of
// being destroyed immediately, resources whose ref-counts reach zero are kept
// alive until all command buffers that reference them have finished executing.
//
// Not thread safe.
class CommandBuffer : public Reffable {
 public:
  enum class Type { kGraphics = 0, kCompute, kTransfer, kEnumCount };

  // Constructors.
  static CommandBufferPtr NewForType(Escher* escher, Type type);
  static CommandBufferPtr NewForGraphics(Escher* escher);
  static CommandBufferPtr NewForCompute(Escher* escher);
  static CommandBufferPtr NewForTransfer(Escher* escher);

  Type type() const { return type_; }

  vk::CommandBuffer vk() const { return vk_; }
  vk::Device vk_device() const { return vk_device_; }
  // TODO(ES-83): deprecated from the get-go.
  impl::CommandBuffer* impl() const { return impl_; }

  // Submits the command buffer on the appropriate queue: the main queue for
  // graphics and compute tasks, and the transfer queue for dedicated transfer
  // operations.
  //
  // TODO(ES-83): this is a placeholder; the submission API will be refined.
  bool Submit(CommandBufferFinishedCallback callback);

  // Wraps vkCmdBeginRenderPass(). Uses |info| to obtain a cached VkRenderPass
  // and VkFramebuffer.
  void BeginRenderPass(const RenderPassInfo& info);

  // Wraps vkCmdEndRenderPass().
  void EndRenderPass();

  // Wraps vkCmdPipelineBarrier(), using a barrier consisting of a single
  // VkImageMemoryBarrier.  Keeps |image| alive while command buffer is pending.
  void ImageBarrier(const ImagePtr& image, vk::ImageLayout old_layout,
                    vk::ImageLayout new_layout,
                    vk::PipelineStageFlags src_stages,
                    vk::AccessFlags src_access,
                    vk::PipelineStageFlags dst_stages,
                    vk::AccessFlags dst_access);

  // Set/dirty a uniform buffer binding that will later be flushed, causing
  // descriptor sets to be written/bound as necessary.  Keeps |buffer| alive
  // while command buffer is pending.
  void BindUniformBuffer(uint32_t set, uint32_t binding,
                         const BufferPtr& buffer);
  void BindUniformBuffer(uint32_t set, uint32_t binding,
                         const BufferPtr& buffer, vk::DeviceSize offset,
                         vk::DeviceSize range);
  void BindUniformBuffer(uint32_t set, uint32_t binding, Buffer* buffer,
                         vk::DeviceSize offset, vk::DeviceSize range);

  // Set/dirty a texture binding that will later be flushed, causing descriptor
  // sets to be written/bound as necessary.  Keeps |texture| alive while command
  // buffer is pending.
  void BindTexture(unsigned set, unsigned binding, Texture* texture);
  void BindTexture(unsigned set, unsigned binding, const TexturePtr& texture) {
    BindTexture(set, binding, texture.get());
  }

  // Set/dirty a vertex buffer binding that will later be flushed, causing
  // descriptor sets to be written/bound as necessary.  Keeps |buffer| alive
  // while command buffer is pending.
  void BindVertices(
      uint32_t binding, Buffer* buffer, vk::DeviceSize offset,
      vk::DeviceSize stride,
      vk::VertexInputRate step_rate = vk::VertexInputRate::eVertex);
  void BindVertices(
      uint32_t binding, const BufferPtr& buffer, vk::DeviceSize offset,
      vk::DeviceSize stride,
      vk::VertexInputRate step_rate = vk::VertexInputRate::eVertex) {
    BindVertices(binding, buffer.get(), offset, stride, step_rate);
  }

  // Sets the current index buffer binding; this happens immediately because
  // index buffer changes never require descriptor sets to be written or new
  // pipelines to be generated.
  void BindIndices(vk::Buffer buffer, vk::DeviceSize offset,
                   vk::IndexType index_type);
  // This variant keeps |buffer| alive while command buffer is pending.
  void BindIndices(const BufferPtr& buffer, vk::DeviceSize offset,
                   vk::IndexType index_type);

  // Set/dirty the attributes that will be used to interpret the vertex buffer
  // at |binding| (see BindVertices() above) when the next draw call is made.
  void SetVertexAttributes(uint32_t binding, uint32_t attrib, vk::Format format,
                           vk::DeviceSize offset) {
    FXL_DCHECK(IsInRenderPass());
    pipeline_state_.SetVertexAttributes(binding, attrib, format, offset);
    SetDirty(kDirtyStaticVertexBit);
  }

  // Wraps vkCmdDrawIndexed(), first flushing any dirty render state; this may
  // cause descriptor sets to be written/bound, or a new pipeline to be created.
  void DrawIndexed(uint32_t index_count, uint32_t instance_count = 1,
                   uint32_t first_index = 0, int32_t vertex_offset = 0,
                   uint32_t first_instance = 0);

  // Wraps vkCmdClearAttachments().  Clears the specified rectangle of the
  // specified attachment (see RenderPassInfo), filling it with the specified
  // values.
  void ClearQuad(uint32_t attachment, const vk::ClearRect& rect,
                 const vk::ClearValue& value, vk::ImageAspectFlags aspect);

  // Convenient way to bring CommandBuffer to a known default state.  See the
  // implementation of SetToDefaultState() for more details; it's basically a
  // big switch statement.
  enum class DefaultState { kOpaque };
  void SetToDefaultState(DefaultState state);

  // Set the ShaderProgram that will be used to obtain the VkPipeline to be used
  // by the next draw-call or compute dispatch.
  void SetShaderProgram(ShaderProgram* program);
  void SetShaderProgram(const ShaderProgramPtr& program) {
    SetShaderProgram(program.get());
  }

  // Set the viewport.  Must be called within a render pass.
  void SetViewport(const vk::Viewport& viewport);

  // Set the scissor rect.  Must be called within a render pass.
  void SetScissor(const vk::Rect2D& rect);

  // The following functions set static state that might result in generation of
  // a new pipeline variant.

  void SetBlendConstants(const float blend_constants[4]);
  void SetBlendEnable(bool blend_enable);
  void SetBlendFactors(vk::BlendFactor src_color_blend,
                       vk::BlendFactor src_alpha_blend,
                       vk::BlendFactor dst_color_blend,
                       vk::BlendFactor dst_alpha_blend);
  void SetBlendFactors(vk::BlendFactor src_blend, vk::BlendFactor dst_blend);
  void SetBlendOp(vk::BlendOp color_blend_op, vk::BlendOp alpha_blend_op);
  void SetBlendOp(vk::BlendOp blend_op);

  void SetCullMode(vk::CullModeFlags cull_mode);

  void SetDepthBias(bool depth_bias_enable);
  void SetDepthBias(float depth_bias_constant, float depth_bias_slope);
  void SetDepthCompareOp(vk::CompareOp depth_compare);
  void SetDepthTestAndWrite(bool depth_test, bool depth_write);

  void SetFrontFace(vk::FrontFace front_face);

  void SetMultisampleState(bool alpha_to_coverage, bool alpha_to_one = false,
                           bool sample_shading = false);

  void SetStencilBackOps(vk::CompareOp stencil_back_compare_op,
                         vk::StencilOp stencil_back_pass,
                         vk::StencilOp stencil_back_fail,
                         vk::StencilOp stencil_back_depth_fail);
  void SetStencilBackReference(uint8_t back_compare_mask,
                               uint8_t back_write_mask, uint8_t back_reference);
  void SetStencilFrontOps(vk::CompareOp stencil_front_compare_op,
                          vk::StencilOp stencil_front_pass,
                          vk::StencilOp stencil_front_fail,
                          vk::StencilOp stencil_front_depth_fail);
  void SetStencilFrontReference(uint8_t front_compare_mask,
                                uint8_t front_write_mask,
                                uint8_t front_reference);
  void SetStencilOps(vk::CompareOp stencil_compare_op,
                     vk::StencilOp stencil_pass, vk::StencilOp stencil_fail,
                     vk::StencilOp stencil_depth_fail);
  void SetStencilTest(bool stencil_test);

  void SetPrimitiveRestart(bool primitive_restart);
  void SetPrimitiveTopology(vk::PrimitiveTopology primitive_topology);
  void SetWireframe(bool wireframe);

  // State.  Clients don't need to worry about this; these are only used
  // internally.  The only reason that they're not private is that they are
  // aggregated in SavedState.
  //
  // TODO(ES-83): SavedState is not yet used.
  // TODO(ES-83): Experiment with making them private except for SavedState,
  // which means that SavedState would be a fully-opaque representation.

  // TODO(ES-83): Not saved in SavedState.  Should it be?  Otherwise, make
  // private?
  struct IndexBindingState {
    vk::Buffer buffer;
    vk::DeviceSize offset;
    vk::IndexType index_type;
  };

  // Resource binding info for a single Vulkan descriptor.  When flushed by
  // FlushRenderState(), the type of the union value is resolved by using the
  // masks in the current impl::DescriptorSetLayout.
  struct DescriptorBindingInfo {
    union {
      vk::DescriptorBufferInfo buffer;
      struct {
        vk::DescriptorImageInfo fp;
        vk::DescriptorImageInfo integer;
      } image;
      vk::BufferView buffer_view;
    };
  };

  // Aggregates bindings for all descriptors in a single descriptor set.  This
  // includes:
  // - the specific Vulkan resource(s) to be bound (samplers, buffers, images)
  // - the IDs of the corresponding Escher resources from which the Vulkan
  //   resources are obtained.
  struct DescriptorSetBindings {
    DescriptorBindingInfo infos[VulkanLimits::kNumBindings];
    uint64_t uids[VulkanLimits::kNumBindings];
    uint64_t secondary_uids[VulkanLimits::kNumBindings];
  };

  // Aggregates bindings for all descriptor sets, as well as push constant data.
  struct ResourceBindings {
    DescriptorSetBindings descriptor_sets[VulkanLimits::kNumDescriptorSets];
    uint8_t push_constant_data[VulkanLimits::kPushConstantSize];
  };

  using PipelineStaticState = CommandBufferPipelineState::StaticState;
  using PipelinePotentialStaticState =
      CommandBufferPipelineState::PotentialStaticState;

  // State that can be changed dynamically without requiring pipeline changes.
  struct DynamicState {
    float depth_bias_constant = 0.0f;
    float depth_bias_slope = 0.0f;
    uint8_t front_compare_mask = 0;
    uint8_t front_write_mask = 0;
    uint8_t front_reference = 0;
    uint8_t back_compare_mask = 0;
    uint8_t back_write_mask = 0;
    uint8_t back_reference = 0;
  };

  // Flags that identify the specific state that is saved in a SavedState (any
  // other state is undefined and should not be used).
  enum SavedStateBits {
    kSavedBindingsBit0 = 1u << 0,
    kSavedBindingsBit1 = 1u << 1,
    kSavedBindingsBit2 = 1u << 2,
    kSavedBindingsBit3 = 1u << 3,
    kSavedViewportBit = 1u << 4,
    kSavedScissorBit = 1u << 5,
    kSavedRenderStateBit = 1u << 6,
    kSavedPushConstantBit = 1u << 7,
  };
  using SavedStateFlags = uint32_t;

  // SavedStateFlags sets aside only 4 bits to indicate which descriptor set
  // bindings are to be saved.  Should we desire a larger number of descriptor
  // sets in the future, more bits must be allocated for this purpose.
  static_assert(
      VulkanLimits::kNumDescriptorSets == 4,
      "Not enough bits to indicate which descriptor set bindings to save.");

  // Saves state so that it can be restored later.
  struct SavedState {
    SavedStateFlags flags = 0;
    ResourceBindings bindings;
    vk::Viewport viewport;
    vk::Rect2D scissor;

    PipelineStaticState static_state;
    PipelinePotentialStaticState potential_static_state;
    DynamicState dynamic_state;
  };

  void SaveState(SavedStateFlags flags, SavedState* state) const;
  void RestoreState(const CommandBuffer::SavedState& state);

 private:
  friend class VulkanTester;

  // Used to track which state must be flushed by FlushGraphicsState().
  enum DirtyBits {
    kDirtyStaticStateBit = 1 << 0,
    kDirtyPipelineBit = 1 << 1,
    kDirtyViewportBit = 1 << 2,
    kDirtyScissorBit = 1 << 3,
    kDirtyDepthBiasBit = 1 << 4,
    // Indicates that the stencil reference value and write/compare masks are
    // dirty, for both front- and back-facing stencil tests.
    kDirtyStencilMasksAndReferenceBit = 1 << 5,
    kDirtyStaticVertexBit = 1 << 6,
    kDirtyPushConstantsBit = 1 << 7,
    // The pipelines that CommandBufferPipelineState::BuildGraphicsPipeline()
    // produces always treats viewport, scissor, stencil, and depth-bias as
    // dynamic state.
    kDirtyDynamicBits = kDirtyViewportBit | kDirtyScissorBit |
                        kDirtyDepthBiasBit | kDirtyStencilMasksAndReferenceBit,
  };
  using DirtyFlags = uint32_t;

  // TODO(ES-83): impl::CommandBuffer is deprecated from the get-go.
  CommandBuffer(EscherWeakPtr escher, Type type,
                impl::CommandBuffer* command_buffer);

  // Sets all flags to dirty, and zeros out DescriptorSetBindings uids.
  void BeginGraphicsOrComputeContext();

  // Called by BeginRenderPass(), calls BeginGraphicsOrComputeContext().
  void BeginGraphics();

  // Called by EndRenderPass(): any time we're not processing graphics commands,
  // we are assumed to be processing compute tasks.  Calls
  // BeginGraphicsOrComputeContext().
  void BeginCompute();

  // Return true if BeginRenderPass() has been called more recently than
  // EndRenderPass().
  bool IsInRenderPass();

  // Called immediately before draw calls are made, e.g. by DrawIndexed().
  // Depending on which dirty flags are set, may call FlushGraphicsPipeline()
  // and FlushDescriptorSet(), as well as calling Vulkan setters for dynamic
  // state such as viewport, scissor, depth-bias, etc.
  void FlushRenderState();

  // Called by FlushRenderState() and FlushComputeState().  Flushes all dirty
  // descriptor sets that are required by the current PipelineLayout.
  void FlushDescriptorSets();
  void FlushDescriptorSet(uint32_t set_index);

  // Called by FlushDescriptorSet() when one or more descriptors in the set must
  // be updated.
  void WriteDescriptors(uint32_t set_index, vk::DescriptorSet vk_set,
                        const impl::DescriptorSetLayout& set_layout);

  // Called when there is the possibility that a pipeline change may be
  // required.  A hash is generated from the currenty-enabled vertex attributes
  // (i.e. those that are used by the current pipeline layout), as well as the
  // current subpass index, and other "static" state.  This hash is used to
  // look up a cached pipeline.  If no pipeline is available, then a new one is
  // built; see CommandBufferPipelineState()::BuildGraphicsPipeline().
  void FlushGraphicsPipeline();

  // Set the specified dirty flag bits.
  void SetDirty(DirtyFlags flags) { dirty_ |= flags; }

  // Return the subset of |flags| that is dirty, and clear only those flags so
  // that they are no longer dirty.
  DirtyFlags GetAndClearDirty(DirtyFlags flags) {
    auto mask = dirty_ & flags;
    dirty_ &= ~flags;
    return mask;
  }

  // Used internally by the various Bind*() methods.
  CommandBuffer::DescriptorSetBindings* GetDescriptorSetBindings(
      uint32_t set_index) {
    FXL_DCHECK(set_index < VulkanLimits::kNumDescriptorSets);
    return &(bindings_.descriptor_sets[set_index]);
  }

  // Used internally by the various Bind*() methods.
  CommandBuffer::DescriptorBindingInfo* GetDescriptorBindingInfo(
      uint32_t set_index, uint32_t binding_index) {
    FXL_DCHECK(binding_index < VulkanLimits::kNumBindings);
    return &(GetDescriptorSetBindings(set_index)->infos[binding_index]);
  }

  // Used internally by the various Bind*() methods.
  CommandBuffer::DescriptorBindingInfo* GetDescriptorBindingInfo(
      CommandBuffer::DescriptorSetBindings* set_bindings,
      uint32_t binding_index) {
    FXL_DCHECK(binding_index < VulkanLimits::kNumBindings);
    return &(set_bindings->infos[binding_index]);
  }

  EscherWeakPtr const escher_;
  Type type_;

  // TODO(ES-83): deprecated from the get-go.
  impl::CommandBuffer* const impl_;
  vk::CommandBuffer vk_;
  vk::Device vk_device_;

  DirtyFlags dirty_ = ~0u;
  uint32_t dirty_descriptor_sets_ = 0;

  bool is_compute_ = false;

  CommandBufferPipelineState pipeline_state_;
  DynamicState dynamic_state_ = {};
  IndexBindingState index_binding_ = {};
  ResourceBindings bindings_ = {};

  impl::FramebufferPtr framebuffer_;

  vk::Pipeline current_vk_pipeline_;
  vk::PipelineLayout current_vk_pipeline_layout_;

  ShaderProgram* current_program_ = nullptr;
  PipelineLayout* current_pipeline_layout_ = nullptr;

  vk::Viewport viewport_ = {};
  vk::Rect2D scissor_ = {};

};  // namespace escher

// Inline function definitions.

#if defined(SET_STATIC_STATE) || defined(SET_STATIC_STATE_ENUM) || \
    defined(SET_POTENTIALLY_STATIC_STATE) || defined(SET_DYNAMIC_STATE)
#error CommandBuffer state macros already defined.
#endif

#define SET_STATIC_STATE(value)                           \
  do {                                                    \
    if (pipeline_state_.static_state()->value != value) { \
      pipeline_state_.static_state()->value = value;      \
      SetDirty(kDirtyStaticStateBit);                     \
    }                                                     \
  } while (0)

#define SET_STATIC_STATE_ENUM(value)                           \
  do {                                                         \
    auto enum_value = EnumCast(value);                         \
    if (pipeline_state_.static_state()->value != enum_value) { \
      pipeline_state_.static_state()->value = enum_value;      \
      SetDirty(kDirtyStaticStateBit);                          \
    }                                                          \
  } while (0)

#define SET_POTENTIALLY_STATIC_STATE(value)                         \
  do {                                                              \
    if (pipeline_state_.potential_static_state()->value != value) { \
      pipeline_state_.potential_static_state()->value = value;      \
      SetDirty(kDirtyStaticStateBit);                               \
    }                                                               \
  } while (0)

#define SET_DYNAMIC_STATE(state, flags)  \
  do {                                   \
    if (dynamic_state_.state != state) { \
      dynamic_state_.state = state;      \
      SetDirty(flags);                   \
    }                                    \
  } while (0)

inline void CommandBuffer::SetViewport(const vk::Viewport& viewport) {
  // Must be called in render pass, because BeginRenderPass() sets the scissor
  // region, and confusion might result if a client didn't realize this and
  // tried to set it outside of a render pass.
  FXL_DCHECK(IsInRenderPass());
  viewport_ = viewport;
  SetDirty(kDirtyViewportBit);
}

inline void CommandBuffer::SetScissor(const vk::Rect2D& rect) {
  // Must be called in render pass, because BeginRenderPass() sets the viewport,
  // and confusion might result if a client didn't realize this and tried to
  // set it outside of a render pass.
  FXL_DCHECK(IsInRenderPass());
  FXL_DCHECK(rect.offset.x >= 0);
  FXL_DCHECK(rect.offset.y >= 0);
  scissor_ = rect;
  SetDirty(kDirtyScissorBit);
}

inline void CommandBuffer::SetDepthTestAndWrite(bool depth_test,
                                                bool depth_write) {
  SET_STATIC_STATE(depth_test);
  SET_STATIC_STATE(depth_write);
}

inline void CommandBuffer::SetWireframe(bool wireframe) {
  SET_STATIC_STATE(wireframe);
}

inline void CommandBuffer::SetDepthCompareOp(vk::CompareOp depth_compare) {
  SET_STATIC_STATE_ENUM(depth_compare);
}

inline void CommandBuffer::SetBlendEnable(bool blend_enable) {
  SET_STATIC_STATE(blend_enable);
}

inline void CommandBuffer::SetBlendFactors(vk::BlendFactor src_color_blend,
                                           vk::BlendFactor src_alpha_blend,
                                           vk::BlendFactor dst_color_blend,
                                           vk::BlendFactor dst_alpha_blend) {
  SET_STATIC_STATE_ENUM(src_color_blend);
  SET_STATIC_STATE_ENUM(dst_color_blend);
  SET_STATIC_STATE_ENUM(src_alpha_blend);
  SET_STATIC_STATE_ENUM(dst_alpha_blend);
}

inline void CommandBuffer::SetBlendFactors(vk::BlendFactor src_blend,
                                           vk::BlendFactor dst_blend) {
  SetBlendFactors(src_blend, src_blend, dst_blend, dst_blend);
}

inline void CommandBuffer::SetBlendOp(vk::BlendOp color_blend_op,
                                      vk::BlendOp alpha_blend_op) {
  SET_STATIC_STATE_ENUM(color_blend_op);
  SET_STATIC_STATE_ENUM(alpha_blend_op);
}

inline void CommandBuffer::SetBlendOp(vk::BlendOp blend_op) {
  SetBlendOp(blend_op, blend_op);
}

inline void CommandBuffer::SetDepthBias(bool depth_bias_enable) {
  SET_STATIC_STATE(depth_bias_enable);
}

inline void CommandBuffer::SetStencilTest(bool stencil_test) {
  SET_STATIC_STATE(stencil_test);
}

inline void CommandBuffer::SetStencilFrontOps(
    vk::CompareOp stencil_front_compare_op, vk::StencilOp stencil_front_pass,
    vk::StencilOp stencil_front_fail, vk::StencilOp stencil_front_depth_fail) {
  SET_STATIC_STATE_ENUM(stencil_front_compare_op);
  SET_STATIC_STATE_ENUM(stencil_front_pass);
  SET_STATIC_STATE_ENUM(stencil_front_fail);
  SET_STATIC_STATE_ENUM(stencil_front_depth_fail);
}

inline void CommandBuffer::SetStencilBackOps(
    vk::CompareOp stencil_back_compare_op, vk::StencilOp stencil_back_pass,
    vk::StencilOp stencil_back_fail, vk::StencilOp stencil_back_depth_fail) {
  SET_STATIC_STATE_ENUM(stencil_back_compare_op);
  SET_STATIC_STATE_ENUM(stencil_back_pass);
  SET_STATIC_STATE_ENUM(stencil_back_fail);
  SET_STATIC_STATE_ENUM(stencil_back_depth_fail);
}

inline void CommandBuffer::SetStencilOps(vk::CompareOp stencil_compare_op,
                                         vk::StencilOp stencil_pass,
                                         vk::StencilOp stencil_fail,
                                         vk::StencilOp stencil_depth_fail) {
  SetStencilFrontOps(stencil_compare_op, stencil_pass, stencil_fail,
                     stencil_depth_fail);
  SetStencilBackOps(stencil_compare_op, stencil_pass, stencil_fail,
                    stencil_depth_fail);
}

inline void CommandBuffer::SetPrimitiveTopology(
    vk::PrimitiveTopology primitive_topology) {
  SET_STATIC_STATE_ENUM(primitive_topology);
}

inline void CommandBuffer::SetPrimitiveRestart(bool primitive_restart) {
  SET_STATIC_STATE(primitive_restart);
}

inline void CommandBuffer::SetMultisampleState(bool alpha_to_coverage,
                                               bool alpha_to_one,
                                               bool sample_shading) {
  SET_STATIC_STATE(alpha_to_coverage);
  SET_STATIC_STATE(alpha_to_one);
  SET_STATIC_STATE(sample_shading);
}

inline void CommandBuffer::SetFrontFace(vk::FrontFace front_face) {
  SET_STATIC_STATE_ENUM(front_face);
}

inline void CommandBuffer::SetCullMode(vk::CullModeFlags vk_cull_mode) {
  auto cull_mode = static_cast<VkCullModeFlags>(vk_cull_mode);
  SET_STATIC_STATE(cull_mode);
}

inline void CommandBuffer::SetBlendConstants(const float blend_constants[4]) {
  SET_POTENTIALLY_STATIC_STATE(blend_constants[0]);
  SET_POTENTIALLY_STATIC_STATE(blend_constants[1]);
  SET_POTENTIALLY_STATIC_STATE(blend_constants[2]);
  SET_POTENTIALLY_STATIC_STATE(blend_constants[3]);
}

inline void CommandBuffer::SetDepthBias(float depth_bias_constant,
                                        float depth_bias_slope) {
  SET_DYNAMIC_STATE(depth_bias_constant, kDirtyDepthBiasBit);
  SET_DYNAMIC_STATE(depth_bias_slope, kDirtyDepthBiasBit);
}

inline void CommandBuffer::SetStencilFrontReference(uint8_t front_compare_mask,
                                                    uint8_t front_write_mask,
                                                    uint8_t front_reference) {
  SET_DYNAMIC_STATE(front_compare_mask, kDirtyStencilMasksAndReferenceBit);
  SET_DYNAMIC_STATE(front_write_mask, kDirtyStencilMasksAndReferenceBit);
  SET_DYNAMIC_STATE(front_reference, kDirtyStencilMasksAndReferenceBit);
}

inline void CommandBuffer::SetStencilBackReference(uint8_t back_compare_mask,
                                                   uint8_t back_write_mask,
                                                   uint8_t back_reference) {
  SET_DYNAMIC_STATE(back_compare_mask, kDirtyStencilMasksAndReferenceBit);
  SET_DYNAMIC_STATE(back_write_mask, kDirtyStencilMasksAndReferenceBit);
  SET_DYNAMIC_STATE(back_reference, kDirtyStencilMasksAndReferenceBit);
}

#undef SET_STATIC_STATE
#undef SET_STATIC_STATE_ENUM
#undef SET_POTENTIALLY_STATIC_STATE
#undef SET_DYNAMIC_STATE

}  // namespace escher

#endif  // LIB_ESCHER_THIRD_PARTY_GRANITE_VK_COMMAND_BUFFER_H_
