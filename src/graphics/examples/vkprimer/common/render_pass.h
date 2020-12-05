// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_EXAMPLES_VKPRIMER_COMMON_RENDER_PASS_H_
#define SRC_GRAPHICS_EXAMPLES_VKPRIMER_COMMON_RENDER_PASS_H_

#include "src/graphics/examples/vkprimer/common/device.h"
#include "src/lib/fxl/macros.h"

#include <vulkan/vulkan.hpp>

namespace vkp {

class RenderPass {
 public:
  RenderPass(std::shared_ptr<vk::Device> device, const vk::Format &image_format, bool offscreen);

  void set_initial_layout(vk::ImageLayout initial_layout) { initial_layout_ = initial_layout; }

  bool Init();
  const vk::RenderPass &get() const { return render_pass_.get(); }

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(RenderPass);

  bool initialized_;
  std::shared_ptr<vk::Device> device_;
  const vk::Format image_format_;
  bool offscreen_;
  vk::ImageLayout initial_layout_ = vk::ImageLayout::eUndefined;

  vk::UniqueRenderPass render_pass_;
};

}  // namespace vkp

#endif  // SRC_GRAPHICS_EXAMPLES_VKPRIMER_COMMON_RENDER_PASS_H_
