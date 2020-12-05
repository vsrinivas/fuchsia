// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_EXAMPLES_VKPROTO_COMMON_COMMAND_BUFFERS_H_
#define SRC_GRAPHICS_EXAMPLES_VKPROTO_COMMON_COMMAND_BUFFERS_H_

#include <memory>
#include <vector>

#include "src/graphics/examples/vkproto/common/command_pool.h"
#include "src/graphics/examples/vkproto/common/device.h"
#include "src/graphics/examples/vkproto/common/framebuffers.h"
#include "src/lib/fxl/macros.h"

#include <vulkan/vulkan.hpp>

namespace vkp {

class CommandBuffers {
 public:
  CommandBuffers(std::shared_ptr<vk::Device> device, std::shared_ptr<CommandPool> vkp_command_pool,
                 const std::vector<vk::UniqueFramebuffer> &framebuffers, const vk::Extent2D &extent,
                 const vk::RenderPass &render_pass, const vk::Pipeline &graphics_pipeline);
  // Allocated and initialize command buffers.  Mutually exclusive with Alloc().
  bool Init();

  // Allocate command buffers from command pool without initialization.
  // May not call Init() after Alloc().  Initialization must be done manually.
  bool Alloc();

  const std::vector<vk::UniqueCommandBuffer> &command_buffers() const { return command_buffers_; }
  const std::vector<vk::UniqueFramebuffer> &framebuffers() const { return params_->framebuffers_; }
  const vk::RenderPass &render_pass() const { return params_->render_pass_; }
  const vk::Extent2D &extent() const { return params_->extent_; }
  const vk::Pipeline &graphics_pipeline() const { return params_->graphics_pipeline_; }

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(CommandBuffers);

  bool initialized_ = false;
  bool allocated_ = false;
  std::shared_ptr<vk::Device> device_;
  std::shared_ptr<vkp::CommandPool> vkp_command_pool_;

  struct InitParams {
    InitParams(const std::vector<vk::UniqueFramebuffer> &framebuffers, const vk::Extent2D &extent,
               const vk::RenderPass &render_pass, const vk::Pipeline &graphics_pipeline);
    const std::vector<vk::UniqueFramebuffer> &framebuffers_;
    vk::Extent2D extent_;
    vk::RenderPass render_pass_;
    vk::Pipeline graphics_pipeline_;
  };
  std::unique_ptr<InitParams> params_;
  std::vector<vk::UniqueCommandBuffer> command_buffers_;
  const size_t num_command_buffers_;
};

}  // namespace vkp

#endif  // SRC_GRAPHICS_EXAMPLES_VKPROTO_COMMON_COMMAND_BUFFERS_H_
