// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_VULKAN_TESTS_VKPRIMER_COMMON_VULKAN_INSTANCE_H_
#define GARNET_LIB_VULKAN_TESTS_VKPRIMER_COMMON_VULKAN_INSTANCE_H_

#include <vector>
#include <vulkan/vulkan.hpp>

#include <src/lib/fxl/macros.h>

#if USE_GLFW
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#endif

class VulkanInstance {
 public:
  VulkanInstance() : initialized_(false) {}
  ~VulkanInstance();

#if USE_GLFW
  bool Init(bool enable_validation, GLFWwindow *window);
#else
  bool Init(bool enable_validation);
#endif

  const vk::UniqueInstance &instance() const;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(VulkanInstance);

  std::vector<const char *> GetExtensions();

#if USE_GLFW
  GLFWwindow *window_;
#endif

  bool initialized_;
  std::vector<const char *> extensions_;
  std::vector<const char *> layers_;

  vk::UniqueInstance instance_;
};

#endif  // GARNET_LIB_VULKAN_TESTS_VKPRIMER_COMMON_VULKAN_INSTANCE_H_
