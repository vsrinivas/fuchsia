// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_EXAMPLES_VKPROTO_FUCHSIA_SURFACE_H_
#define SRC_GRAPHICS_EXAMPLES_VKPROTO_FUCHSIA_SURFACE_H_

#include <memory>

#include <vulkan/vulkan.h>

#include "instance.h"
#include "src/lib/fxl/macros.h"

namespace vkp {

class Surface {
 public:
  explicit Surface(std::shared_ptr<vk::Instance> instance);
  ~Surface();

  bool Init();
  const VkSurfaceKHR& get() const { return surface_; }

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(Surface);

  bool initialized_;
  std::shared_ptr<vk::Instance> instance_;
  VkSurfaceKHR surface_;
};

}  // namespace vkp

#endif  // SRC_GRAPHICS_EXAMPLES_VKPROTO_FUCHSIA_SURFACE_H_
