// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vulkan_logical_device.h"

#include "utils.h"
#include "vulkan_layer.h"
#include "vulkan_physical_device.h"
#include "vulkan_queue.h"
#include "vulkan_swapchain.h"

VulkanLogicalDevice::VulkanLogicalDevice(const VkPhysicalDevice &phys_device,
                                         const VkSurfaceKHR &surface,
                                         const bool enable_validation)
    : initialized_(false),
      device_(VK_NULL_HANDLE),
      enable_validation_(enable_validation),
      queue_(nullptr) {
  params_ = std::make_unique<SurfacePhysDeviceParams>(phys_device, surface);
}

VulkanLogicalDevice::~VulkanLogicalDevice() {
  if (initialized_) {
    vkDestroyDevice(device_, nullptr);
    initialized_ = false;
  }
}

bool VulkanLogicalDevice::Init() {
  if (initialized_) {
    RTN_MSG(false, "Logical device already initialized.\n");
  }

  std::vector<uint32_t> indices;
  if (!VulkanQueue::FindGraphicsQueueFamilies(params_->phys_device_,
                                              params_->surface_, &indices)) {
    return false;
  }

  float queue_priority = 1.0f;
  VkDeviceQueueCreateInfo queue_create_info = {
      .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
      .queueCount = 1,
      .queueFamilyIndex = indices[0],
      .pQueuePriorities = &queue_priority,
  };

  VkPhysicalDeviceFeatures device_features = {};

  std::vector<const char *> exts;
  VulkanPhysicalDevice::AppendRequiredPhysDeviceExts(&exts);

  VkDeviceCreateInfo device_create_info = {
      .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
      .enabledExtensionCount = static_cast<uint32_t>(exts.size()),
      .pQueueCreateInfos = &queue_create_info,
      .pEnabledFeatures = &device_features,
      .ppEnabledExtensionNames = exts.data(),
      .queueCreateInfoCount = 1,
  };

  if (enable_validation_) {
    VulkanLayer::AppendRequiredDeviceLayers(&layers_);
    device_create_info.enabledLayerCount = layers_.size();
  } else {
    device_create_info.enabledLayerCount = 0;
  }

  auto err = vkCreateDevice(params_->phys_device_, &device_create_info, nullptr,
                            &device_);
  if (VK_SUCCESS != err) {
    RTN_MSG(false, "VK Error: 0x%x - Failed to create device.\n", err);
  }

  vkGetDeviceQueue(device_, indices[0], 0, &queue_);
  params_.reset();
  initialized_ = true;

  return true;
}

VkDevice VulkanLogicalDevice::device() const {
  if (!initialized_) {
    RTN_MSG(device_, "Can't retrieve device.  Not initialized.\n");
  }
  return device_;
}

VkQueue VulkanLogicalDevice::queue() const {
  if (!initialized_) {
    RTN_MSG(queue_, "Can't retrieve queue.  Not initialized.\n");
  }
  return queue_;
}
