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
  explicit Instance(bool enable_validation) : enable_validation_(enable_validation) {}
  ~Instance();

#if USE_GLFW
  bool Init(GLFWwindow *window);
#else
  bool Init();
#endif

  const vk::Instance &get() const;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(Instance);
  bool ConfigureDebugMessenger();
  std::vector<const char *> GetExtensions();

#if USE_GLFW
  GLFWwindow *window_;
#endif

  bool initialized_ = false;
  bool enable_validation_ = false;
  std::vector<const char *> extensions_;
  std::vector<const char *> layers_;
  vk::UniqueHandle<vk::DebugUtilsMessengerEXT, vk::DispatchLoaderDynamic> debug_messenger_;
  vk::DispatchLoaderDynamic dispatch_loader_;

  vk::UniqueInstance instance_;
};

}  // namespace vkp

#endif  // SRC_GRAPHICS_EXAMPLES_VKPRIMER_COMMON_INSTANCE_H_
