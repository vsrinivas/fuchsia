// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/examples/vkprimer/common/device.h"

#include "src/graphics/examples/vkprimer/common/physical_device.h"
#include "src/graphics/examples/vkprimer/common/swapchain.h"
#include "src/graphics/examples/vkprimer/common/utils.h"

namespace vkp {

Device::Device(const vk::PhysicalDevice &phys_device, const VkSurfaceKHR &surface)
    : initialized_(false), queue_(nullptr) {
  params_ = std::make_unique<SurfacePhysDeviceParams>(phys_device, surface);
}

bool Device::Init() {
  RTN_IF_MSG(false, initialized_, "Logical device already initialized.\n");

  std::vector<uint32_t> indices;
  if (!FindGraphicsQueueFamilies(params_->phys_device_, params_->surface_, &indices)) {
    return false;
  }

  float queue_priority = 1.0f;
  vk::DeviceQueueCreateInfo queue_info;
  queue_info.queueCount = 1;
  queue_info.queueFamilyIndex = indices[0];
  queue_info.pQueuePriorities = &queue_priority;

  queue_family_index_ = indices[0];

  std::vector<const char *> exts;
  PhysicalDevice::AppendRequiredPhysDeviceExts(&exts);

  vk::PhysicalDeviceFeatures device_features;
  vk::DeviceCreateInfo device_info;
  device_info.setEnabledExtensionCount(static_cast<uint32_t>(exts.size()));
  device_info.queueCreateInfoCount = 1;
  device_info.pQueueCreateInfos = &queue_info;
  device_info.pEnabledFeatures = &device_features;
  device_info.setPpEnabledExtensionNames(exts.data());
  device_info.enabledLayerCount = 0;

  auto [r_device, device] = params_->phys_device_.createDeviceUnique(device_info);
  RTN_IF_VKH_ERR(false, r_device, "Failed to create logical device.\n");
  device_ = std::move(device);

  queue_ = device_->getQueue(indices[0], 0);
  params_.reset();
  initialized_ = true;

  return true;
}

const vk::Device &Device::get() const {
  RTN_IF_MSG(device_.get(), !initialized_, "Can't retrieve device.  Not initialized.\n");
  return device_.get();
}

vk::Queue Device::queue() const {
  RTN_IF_MSG(queue_, !initialized_, "Can't retrieve queue.  Not initialized.\n");
  return queue_;
}

}  // namespace vkp
