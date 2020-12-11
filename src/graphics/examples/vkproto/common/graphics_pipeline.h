// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_EXAMPLES_VKPROTO_COMMON_GRAPHICS_PIPELINE_H_
#define SRC_GRAPHICS_EXAMPLES_VKPROTO_COMMON_GRAPHICS_PIPELINE_H_

#include "src/graphics/examples/vkproto/common/device.h"
#include "src/graphics/examples/vkproto/common/render_pass.h"
#include "src/lib/fxl/macros.h"

#include <vulkan/vulkan.hpp>

namespace vkp {

class GraphicsPipeline {
 public:
  GraphicsPipeline(std::shared_ptr<vk::Device> device, const vk::Extent2D &extent,
                   std::shared_ptr<RenderPass> vkp_render_pass);
  ~GraphicsPipeline();

  bool Init();
  const vk::Pipeline &get() const;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(GraphicsPipeline);

  bool initialized_;
  std::shared_ptr<vk::Device> device_;
  const vk::Extent2D extent_;
  std::shared_ptr<RenderPass> vkp_render_pass_;

  vk::PipelineLayout pipeline_layout_;
  vk::Pipeline pipeline_;
};

}  // namespace vkp

#endif  // SRC_GRAPHICS_EXAMPLES_VKPROTO_COMMON_GRAPHICS_PIPELINE_H_
