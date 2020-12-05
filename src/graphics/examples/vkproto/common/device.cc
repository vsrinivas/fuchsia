// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/examples/vkproto/common/device.h"

#include "src/graphics/examples/vkproto/common/physical_device.h"
#include "src/graphics/examples/vkproto/common/swapchain.h"
#include "src/graphics/examples/vkproto/common/utils.h"

namespace vkp {

Device::Device(const vk::PhysicalDevice &phys_device, VkSurfaceKHR surface)
    : initialized_(false), queue_(nullptr) {
  params_ = std::make_unique<SurfacePhysDeviceParams>(phys_device, surface);
}

Device::~Device() {
  if (initialized_) {
    device_.reset();
    initialized_ = false;
  }
}

bool Device::Init() {
  RTN_IF_MSG(false, initialized_, "Logical device already initialized.\n");

  if (!FindGraphicsQueueFamilyIndex(params_->phys_device_, params_->surface_,
                                    &queue_family_index_)) {
    return false;
  }

  float queue_priority = 1.0f;
  vk::DeviceQueueCreateInfo queue_info;
  queue_info.queueCount = 1;
  queue_info.queueFamilyIndex = queue_family_index_;
  queue_info.pQueuePriorities = &queue_priority;

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

  vk::Device *device = new vk::Device;
  auto r_device =
      params_->phys_device_.createDevice(&device_info, nullptr /* pAllocator */, device);
  if (vk::Result::eSuccess != r_device) {
    delete device;
    RTN_MSG(false, "Failed to create logical device.\n");
  }
  device_.reset(device, [](vk::Device *d) {
    if (d) {
      d->destroy();
      delete d;
    }
  });

  queue_ = device_->getQueue(queue_family_index_, 0);
  params_.reset();
  initialized_ = true;

  return true;
}

std::shared_ptr<vk::Device> Device::shared() {
  RTN_IF_MSG(nullptr, !initialized_, "Device hasn't been initialized");
  return device_;
}

const vk::Device &Device::get() const {
  RTN_IF_MSG(*device_, !initialized_, "Can't retrieve device.  Not initialized.\n");
  return *device_;
}

vk::Queue Device::queue() const {
  RTN_IF_MSG(queue_, !initialized_, "Can't retrieve queue.  Not initialized.\n");
  return queue_;
}

}  // namespace vkp
