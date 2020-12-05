// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_EXAMPLES_VKPROTO_COMMON_PHYSICAL_DEVICE_H_
#define SRC_GRAPHICS_EXAMPLES_VKPROTO_COMMON_PHYSICAL_DEVICE_H_

#include <memory>
#include <vector>

#include "src/graphics/examples/vkproto/common/instance.h"
#include "src/lib/fxl/macros.h"

#include <vulkan/vulkan.hpp>

namespace vkp {

class PhysicalDevice {
 public:
  explicit PhysicalDevice(std::shared_ptr<vk::Instance> instance, VkSurfaceKHR surface = nullptr);

  bool Init();
  const vk::PhysicalDevice &get() const;

  static void AppendRequiredPhysDeviceExts(std::vector<const char *> *exts);

 private:
  PhysicalDevice() = delete;
  FXL_DISALLOW_COPY_AND_ASSIGN(PhysicalDevice);

  bool initialized_;
  std::shared_ptr<vk::Instance> instance_;
  VkSurfaceKHR surface_ = nullptr;

  vk::PhysicalDevice physical_device_;
};

}  // namespace vkp

#endif  // SRC_GRAPHICS_EXAMPLES_VKPROTO_COMMON_PHYSICAL_DEVICE_H_
