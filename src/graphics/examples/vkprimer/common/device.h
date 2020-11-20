// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_EXAMPLES_VKPRIMER_COMMON_DEVICE_H_
#define SRC_GRAPHICS_EXAMPLES_VKPRIMER_COMMON_DEVICE_H_

#include <vector>

#include "src/graphics/examples/vkprimer/common/surface_phys_device_params.h"
#include "src/lib/fxl/macros.h"

#include <vulkan/vulkan.hpp>

namespace vkp {

class Device {
 public:
  Device(const vk::PhysicalDevice &phys_device, const VkSurfaceKHR &surface,
         const bool enable_validation);

  bool Init();
  const vk::Device &get() const;
  vk::Queue queue() const;
  uint32_t queue_family_index() const { return queue_family_index_; }

 private:
  FXL_DISALLOW_COPY_ASSIGN_AND_MOVE(Device);

  bool AssignSuitableDevice(const std::vector<VkDevice> &devices);

  bool initialized_;
  std::unique_ptr<SurfacePhysDeviceParams> params_;
  const bool enable_validation_;
  std::vector<const char *> layers_;

  // Queue with support for both drawing and presentation.
  vk::Queue queue_;

  uint32_t queue_family_index_{};

  vk::UniqueDevice device_;
};

}  // namespace vkp

#endif  // SRC_GRAPHICS_EXAMPLES_VKPRIMER_COMMON_DEVICE_H_
