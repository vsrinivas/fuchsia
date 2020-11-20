// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_EXAMPLES_VKPRIMER_COMMON_PHYSICAL_DEVICE_H_
#define SRC_GRAPHICS_EXAMPLES_VKPRIMER_COMMON_PHYSICAL_DEVICE_H_

#include <memory>
#include <vector>

#include "src/graphics/examples/vkprimer/common/instance.h"
#include "src/lib/fxl/macros.h"

#include <vulkan/vulkan.hpp>

namespace vkp {

class PhysicalDevice {
 public:
  PhysicalDevice(std::shared_ptr<Instance> vkp_instance, const VkSurfaceKHR &surface);

  bool Init();
  const vk::PhysicalDevice &get() const;

  static void AppendRequiredPhysDeviceExts(std::vector<const char *> *exts);

 private:
  PhysicalDevice() = delete;
  FXL_DISALLOW_COPY_AND_ASSIGN(PhysicalDevice);

  bool initialized_;
  std::shared_ptr<Instance> vkp_instance_;

  struct InitParams {
    InitParams(const VkSurfaceKHR &surface);
    const VkSurfaceKHR surface_;
  };
  std::unique_ptr<InitParams> params_;

  vk::PhysicalDevice phys_device_;
};

}  // namespace vkp

#endif  // SRC_GRAPHICS_EXAMPLES_VKPRIMER_COMMON_PHYSICAL_DEVICE_H_
