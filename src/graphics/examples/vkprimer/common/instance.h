// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_EXAMPLES_VKPRIMER_COMMON_INSTANCE_H_
#define SRC_GRAPHICS_EXAMPLES_VKPRIMER_COMMON_INSTANCE_H_

#include <vector>

#include "src/lib/fxl/macros.h"

#include <vulkan/vulkan.hpp>

#if USE_GLFW
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#endif

namespace vkp {

class Instance {
 public:
  Instance() {}
  ~Instance();

#if USE_GLFW
  bool Init(bool enable_validation, GLFWwindow *window);
#else
  bool Init(bool enable_validation);
#endif

  const vk::Instance &get() const;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(Instance);

  std::vector<const char *> GetExtensions();

#if USE_GLFW
  GLFWwindow *window_;
#endif

  bool initialized_ = false;
  std::vector<const char *> extensions_;
  std::vector<const char *> layers_;

  vk::UniqueInstance instance_;
};

}  // namespace vkp

#endif  // SRC_GRAPHICS_EXAMPLES_VKPRIMER_COMMON_INSTANCE_H_
