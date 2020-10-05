// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vulkan_logical_device.h"

#include "utils.h"
#include "vulkan_layer.h"
#include "vulkan_physical_device.h"
#include "vulkan_swapchain.h"

VulkanLogicalDevice::VulkanLogicalDevice(const vk::PhysicalDevice &phys_device,
                                         const VkSurfaceKHR &surface, const bool enable_validation)
    : initialized_(false), enable_validation_(enable_validation), queue_(nullptr) {
  params_ = std::make_unique<SurfacePhysDeviceParams>(phys_device, surface);
}

bool VulkanLogicalDevice::Init() {
  if (initialized_) {
    RTN_MSG(false, "Logical device already initialized.\n");
  }

  std::vector<uint32_t> indices;
  if (!vkp::FindGraphicsQueueFamilies(params_->phys_device_, params_->surface_, &indices)) {
    return false;
  }

  float queue_priority = 1.0f;
  vk::DeviceQueueCreateInfo queue_info;
  queue_info.queueCount = 1;
  queue_info.queueFamilyIndex = indices[0];
  queue_info.pQueuePriorities = &queue_priority;

  queue_family_index_ = indices[0];

  std::vector<const char *> exts;
  VulkanPhysicalDevice::AppendRequiredPhysDeviceExts(&exts);

  vk::PhysicalDeviceFeatures device_features;
  vk::DeviceCreateInfo device_info;
  device_info.setEnabledExtensionCount(static_cast<uint32_t>(exts.size()));
  device_info.queueCreateInfoCount = 1;
  device_info.pQueueCreateInfos = &queue_info;
  device_info.pEnabledFeatures = &device_features;
  device_info.setPpEnabledExtensionNames(exts.data());

  if (enable_validation_) {
    VulkanLayer::AppendRequiredDeviceLayers(&layers_);
    device_info.setEnabledLayerCount(static_cast<uint32_t>(layers_.size()));
  } else {
    device_info.enabledLayerCount = 0;
  }

  auto rv = params_->phys_device_.createDeviceUnique(device_info);
  device_ = std::move(rv.value);
  if (vk::Result::eSuccess != rv.result) {
    RTN_MSG(false, "VK Error: 0x%x - Failed to create logical device.\n", rv.result);
  }

  queue_ = device_->getQueue(indices[0], 0);
  params_.reset();
  initialized_ = true;

  return true;
}

const vk::UniqueDevice &VulkanLogicalDevice::device() const {
  if (!initialized_) {
    RTN_MSG(device_, "Can't retrieve device.  Not initialized.\n");
  }
  return device_;
}

vk::Queue VulkanLogicalDevice::queue() const {
  if (!initialized_) {
    RTN_MSG(queue_, "Can't retrieve queue.  Not initialized.\n");
  }
  return queue_;
}
