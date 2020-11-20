// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_EXAMPLES_VKPRIMER_COMMON_PIPELINE_H_
#define SRC_GRAPHICS_EXAMPLES_VKPRIMER_COMMON_PIPELINE_H_

#include "src/graphics/examples/vkprimer/common/device.h"
#include "src/graphics/examples/vkprimer/common/render_pass.h"
#include "src/lib/fxl/macros.h"

#include <vulkan/vulkan.hpp>

namespace vkp {

class Pipeline {
 public:
  Pipeline(std::shared_ptr<Device> vkp_device, const vk::Extent2D &extent,
           std::shared_ptr<RenderPass> vkp_render_pass);
  ~Pipeline();

  bool Init();
  const vk::Pipeline &get() const;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(Pipeline);

  bool initialized_;
  std::shared_ptr<Device> vkp_device_;
  const vk::Extent2D extent_;
  std::shared_ptr<RenderPass> vkp_render_pass_;

  vk::PipelineLayout pipeline_layout_;
  vk::Pipeline pipeline_;
};

}  // namespace vkp

#endif  // SRC_GRAPHICS_EXAMPLES_VKPRIMER_COMMON_PIPELINE_H_
