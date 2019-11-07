// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_EXAMPLES_VKPRIMER_GLFW_VULKAN_SURFACE_H_
#define SRC_GRAPHICS_EXAMPLES_VKPRIMER_GLFW_VULKAN_SURFACE_H_

// clang-format off
// vulkan.h must be included before glfw3.h.
#include "vulkan/vulkan.h"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
// clang-format on

#include "vulkan_instance.h"

class VulkanSurface {
 public:
  VulkanSurface(std::shared_ptr<VulkanInstance> instance, GLFWwindow *window);
  ~VulkanSurface();

  bool Init();
  const VkSurfaceKHR &surface() const { return surface_; }

 private:
  bool initialized_;
  std::shared_ptr<VulkanInstance> instance_;
  GLFWwindow *window_;
  VkSurfaceKHR surface_;
};

#endif  // SRC_GRAPHICS_EXAMPLES_VKPRIMER_GLFW_VULKAN_SURFACE_H_
