// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_EXAMPLES_VKPRIMER_COMMON_COMMAND_POOL_H_
#define SRC_GRAPHICS_EXAMPLES_VKPRIMER_COMMON_COMMAND_POOL_H_

#include "src/graphics/examples/vkprimer/common/device.h"
#include "src/graphics/examples/vkprimer/common/surface_phys_device_params.h"
#include "src/lib/fxl/macros.h"

#include <vulkan/vulkan.hpp>

namespace vkp {

class CommandPool {
 public:
  CommandPool(std::shared_ptr<Device> vkp_device, vk::PhysicalDevice phys_device,
              const VkSurfaceKHR &surface);

  bool Init();
  const vk::CommandPool &get() const { return command_pool_.get(); }

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(CommandPool);

  bool initialized_;
  std::shared_ptr<Device> vkp_device_;
  std::unique_ptr<SurfacePhysDeviceParams> params_;

  vk::UniqueCommandPool command_pool_;
};

}  // namespace vkp

#endif  // SRC_GRAPHICS_EXAMPLES_VKPRIMER_COMMON_COMMAND_POOL_H_
