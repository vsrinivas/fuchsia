// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_VULKAN_TESTS_VKPRIMER_FUCHSIA_VULKAN_SURFACE_H_
#define GARNET_LIB_VULKAN_TESTS_VKPRIMER_FUCHSIA_VULKAN_SURFACE_H_

#include <vulkan/vulkan.h>

#include <memory>

#include "src/lib/fxl/macros.h"
#include "vulkan_instance.h"

class VulkanSurface {
 public:
  VulkanSurface(std::shared_ptr<VulkanInstance> instance);
  ~VulkanSurface();

  bool Init();
  const VkSurfaceKHR& surface() const { return surface_; }

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(VulkanSurface);

  bool initialized_;
  std::shared_ptr<VulkanInstance> instance_;
  VkSurfaceKHR surface_;
};

#endif  // GARNET_LIB_VULKAN_TESTS_VKPRIMER_FUCHSIA_VULKAN_SURFACE_H_
