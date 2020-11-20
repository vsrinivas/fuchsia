// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_EXAMPLES_VKPRIMER_COMMON_COMMAND_BUFFERS_H_
#define SRC_GRAPHICS_EXAMPLES_VKPRIMER_COMMON_COMMAND_BUFFERS_H_

#include <memory>
#include <vector>

#include "src/graphics/examples/vkprimer/common/command_pool.h"
#include "src/graphics/examples/vkprimer/common/device.h"
#include "src/graphics/examples/vkprimer/common/framebuffers.h"
#include "src/lib/fxl/macros.h"

#include <vulkan/vulkan.hpp>

namespace vkp {

class CommandBuffers {
 public:
  CommandBuffers(std::shared_ptr<Device> vkp_device, std::shared_ptr<CommandPool> vkp_command_pool,
                 const std::vector<vk::UniqueFramebuffer> &framebuffers, const vk::Extent2D &extent,
                 const vk::RenderPass &render_pass, const vk::Pipeline &graphics_pipeline);

  void set_image_for_foreign_transition(vk::Image image) { image_for_foreign_transition_ = image; }

  void set_queue_family(uint32_t queue_family) { queue_family_ = queue_family; }

  bool Init();
  const std::vector<vk::UniqueCommandBuffer> &command_buffers() const { return command_buffers_; }

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(CommandBuffers);

  void AddForeignTransitionImageBarriers(const vk::CommandBuffer &command_buffer);

  bool initialized_;
  std::shared_ptr<Device> vkp_device_;
  std::shared_ptr<CommandPool> vkp_command_pool_;

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
  vk::Image image_for_foreign_transition_;
  uint32_t queue_family_{};
};

}  // namespace vkp

#endif  // SRC_GRAPHICS_EXAMPLES_VKPRIMER_COMMON_COMMAND_BUFFERS_H_
