// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_VULKAN_TESTS_VKPRIMER_COMMON_VULKAN_PHYSICAL_DEVICE_H_
#define GARNET_LIB_VULKAN_TESTS_VKPRIMER_COMMON_VULKAN_PHYSICAL_DEVICE_H_

#include <memory>
#include <src/lib/fxl/macros.h>
#include <vector>
#include <vulkan/vulkan.hpp>

#include "vulkan_instance.h"

class VulkanPhysicalDevice {
 public:
  VulkanPhysicalDevice(std::shared_ptr<VulkanInstance> instance, const VkSurfaceKHR &surface);

  bool Init();
  vk::PhysicalDevice phys_device() const;

  static void AppendRequiredPhysDeviceExts(std::vector<const char *> *exts);

 private:
  VulkanPhysicalDevice() = delete;
  FXL_DISALLOW_COPY_AND_ASSIGN(VulkanPhysicalDevice);

  bool initialized_;
  std::shared_ptr<VulkanInstance> instance_;

  struct InitParams {
    InitParams(const VkSurfaceKHR &surface);
    const VkSurfaceKHR surface_;
  };
  std::unique_ptr<InitParams> params_;

  vk::PhysicalDevice phys_device_;
};

#endif  // GARNET_LIB_VULKAN_TESTS_VKPRIMER_COMMON_VULKAN_PHYSICAL_DEVICE_H_
