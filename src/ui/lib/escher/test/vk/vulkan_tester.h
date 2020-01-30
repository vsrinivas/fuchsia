// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_TEST_VK_VULKAN_TESTER_H_
#define SRC_UI_LIB_ESCHER_TEST_VK_VULKAN_TESTER_H_

#include "src/ui/lib/escher/vk/command_buffer.h"

namespace escher {

class VulkanTester {
 public:
  using DirtyBits = CommandBuffer::DirtyBits;
  using DirtyFlags = CommandBuffer::DirtyFlags;

  template <typename CommandBufferT>  // CommandBuffer* or CommandBufferPtr
  static CommandBufferPipelineState::StaticState* GetStaticState(CommandBufferT cb) {
    return &cb->pipeline_state_.static_state_;
  }

  template <typename CommandBufferT>  // CommandBuffer* or CommandBufferPtr
  static void SetDirty(CommandBufferT cb, CommandBuffer::DirtyFlags flags) {
    cb->SetDirty(flags);
  }

  template <typename CommandBufferT>  // CommandBuffer* or CommandBufferPtr
  static CommandBuffer::DirtyFlags GetAndClearDirty(CommandBufferT cb,
                                                    CommandBuffer::DirtyFlags flags) {
    return cb->GetAndClearDirty(flags);
  }

  template <typename CommandBufferT>  // CommandBuffer* or CommandBufferPtr
  static CommandBuffer::DirtyFlags GetDirty(CommandBufferT cb,
                                            CommandBuffer::DirtyFlags flags = ~0u) {
    return cb->dirty_ & flags;
  }

  template <typename CommandBufferT>  // CommandBuffer* or CommandBufferPtr
  static vk::Pipeline GetCurrentVkPipeline(CommandBufferT cb) {
    return cb->current_vk_pipeline_;
  }

  // Obtain the vk::Pipeline that would be obtained by FlushGraphicsPipeline(),
  // but don't test/clear dirty state, bind the pipeline, etc.
  template <typename CommandBufferT>  // CommandBuffer* or CommandBufferPtr
  static vk::Pipeline ObtainGraphicsPipeline(CommandBufferT cb) {
    FXL_DCHECK(cb->current_pipeline_layout_);
    FXL_DCHECK(cb->current_program_);
    return cb->pipeline_state_.FlushGraphicsPipeline(cb->current_pipeline_layout_,
                                                     cb->current_program_);
  }
};

}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_TEST_VK_VULKAN_TESTER_H_
