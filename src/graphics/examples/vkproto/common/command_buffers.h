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
  static constexpr std::array<float, 4> magenta_s = {0.5f, 0.0f, 0.5f, 1.0f};
  CommandBuffers(std::shared_ptr<vk::Device> device, std::shared_ptr<CommandPool> vkp_command_pool,
                 const std::vector<vk::UniqueFramebuffer> &framebuffers,
                 const vk::Pipeline &graphics_pipeline, const vk::RenderPass &render_pass,
                 const vk::Extent2D &extent, const std::array<float, 4> &clear_color = magenta_s,
                 const vk::CommandBufferUsageFlags &usage_flags =
                     vk::CommandBufferUsageFlagBits::eSimultaneousUse,
                 const vk::CommandBufferLevel &level = vk::CommandBufferLevel::ePrimary);

  // Allocated and initialize command buffers.  Mutually exclusive with Alloc().
  bool Init();

  // Allocate command buffers from command pool without initialization.
  // May not call Init() after Alloc().  Initialization must be done manually.
  bool Alloc();

  const std::vector<vk::UniqueCommandBuffer> &command_buffers() const { return command_buffers_; }
  const std::vector<vk::UniqueFramebuffer> &framebuffers() const { return framebuffers_; }
  const vk::RenderPass &render_pass() const { return render_pass_; }
  const vk::Pipeline &graphics_pipeline() const { return graphics_pipeline_; }
  const vk::Extent2D &extent() const { return extent_; }

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(CommandBuffers);

  bool initialized_ = false;
  bool allocated_ = false;
  std::shared_ptr<vk::Device> device_;
  std::shared_ptr<vkp::CommandPool> vkp_command_pool_;
  const std::vector<vk::UniqueFramebuffer> &framebuffers_;
  const size_t num_command_buffers_;
  vk::Pipeline graphics_pipeline_;
  vk::RenderPass render_pass_;
  vk::Extent2D extent_;
  std::array<float, 4> clear_color_;
  vk::CommandBufferUsageFlags usage_flags_;
  vk::CommandBufferLevel level_;

  std::vector<vk::UniqueCommandBuffer> command_buffers_;
};

}  // namespace vkp

#endif  // SRC_GRAPHICS_EXAMPLES_VKPROTO_COMMON_COMMAND_BUFFERS_H_
