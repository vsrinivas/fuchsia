// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "vulkan_context.h"

#include <stddef.h>

#include <utility>

#include "src/graphics/tests/common/utils.h"

#include "vulkan/vulkan.hpp"

VulkanContext::VulkanContext(const vk::InstanceCreateInfo &instance_info,
                             size_t physical_device_index, const vk::DeviceCreateInfo &device_info,
                             const vk::DeviceQueueCreateInfo &queue_info,
                             const vk::QueueFlagBits &queue_flag_bits,
                             vk::Optional<const vk::AllocationCallbacks> allocator)
    : instance_info_(instance_info),
      physical_device_index_(physical_device_index),
      queue_family_index_(kInvalidQueueFamily),
      queue_flag_bits_(queue_flag_bits),
      queue_info_(queue_info),
      device_info_(device_info),
      allocator_(allocator) {}

VulkanContext::VulkanContext(size_t physical_device_index, const vk::QueueFlagBits &queue_flag_bits,
                             vk::Optional<const vk::AllocationCallbacks> allocator)
    : physical_device_index_(physical_device_index),
      queue_family_index_(kInvalidQueueFamily),
      queue_flag_bits_(queue_flag_bits),
      queue_info_(vk::DeviceQueueCreateFlags(), queue_family_index_, 1 /* queueCount */,
                  &queue_priority_),
      allocator_(allocator) {}

bool VulkanContext::InitInstance() {
  if (instance_initialized_) {
    RTN_MSG(false, "Instance is already initialized.\n");
  }
  vk::ResultValue<vk::UniqueInstance> rv_instance(vk::Result::eNotReady, vk::UniqueInstance{});
  if (allocator_) {
    // Verify valid use of callbacks.
    if (!(allocator_->pfnAllocation && allocator_->pfnReallocation && allocator_->pfnFree)) {
      RTN_MSG(false, "Required allocator function is missing.\n");
    }
    if (allocator_->pfnInternalAllocation && !allocator_->pfnInternalFree) {
      RTN_MSG(false, "pfnInternalAllocation defined without pfnInternalFree.\n");
    }
    if (allocator_->pfnInternalFree && !allocator_->pfnInternalAllocation) {
      RTN_MSG(false, "pfnInternalFree defined without pfnInternalAllocation.\n");
    }
    rv_instance = vk::createInstanceUnique(instance_info_, allocator_);
  } else {
    rv_instance = vk::createInstanceUnique(instance_info_);
  }

  if (vk::Result::eSuccess != rv_instance.result) {
    RTN_MSG(false, "VK Error: 0x%x - Create instance.\n", rv_instance.result);
  }
  instance_ = std::move(rv_instance.value);
  instance_initialized_ = true;
  return true;
}

bool VulkanContext::InitQueueFamily() {
  if (!instance_initialized_) {
    RTN_MSG(false, "Instance must be initialized before queue family.\n");
  }
  if (queue_family_initialized_) {
    RTN_MSG(false, "Queue family is already initialized.\n");
  }
  auto [r_physical_devices, physical_devices] = instance_->enumeratePhysicalDevices();
  if (vk::Result::eSuccess != r_physical_devices || physical_devices.empty()) {
    RTN_MSG(false, "VK Error: 0x%x - No physical device found.\n", r_physical_devices);
  }
  physical_device_ = physical_devices[physical_device_index_];

  const auto queue_families = physical_device_.getQueueFamilyProperties();
  queue_family_index_ = queue_families.size();
  for (size_t i = 0; i < queue_families.size(); ++i) {
    if (queue_families[i].queueFlags & queue_flag_bits_) {
      queue_family_index_ = i;
      break;
    }
  }

  if (static_cast<size_t>(queue_family_index_) == queue_families.size()) {
    queue_family_index_ = kInvalidQueueFamily;
    RTN_MSG(false, "Couldn't find an appropriate queue family.\n");
  }
  queue_info_.queueFamilyIndex = queue_family_index_;
  queue_family_initialized_ = true;
  return true;
}

bool VulkanContext::InitDevice() {
  if (!queue_family_initialized_) {
    RTN_MSG(false, "Queue family must be initialized before device.\n");
  }
  if (device_initialized_) {
    RTN_MSG(false, "Device is already initialized.\n");
  }
  vk::ResultValue<vk::UniqueDevice> rv_device(vk::Result::eNotReady, vk::UniqueDevice{});
  if (allocator_) {
    rv_device = physical_device_.createDeviceUnique(device_info_, allocator_);
  } else {
    rv_device = physical_device_.createDeviceUnique(device_info_);
  }
  if (vk::Result::eSuccess != rv_device.result) {
    RTN_MSG(false, "VK Error: 0x%x - Create device.\n", rv_device.result);
  }
  device_ = std::move(rv_device.value);
  queue_ = device_->getQueue(queue_family_index_, 0);
  device_initialized_ = true;
  return true;
}

bool VulkanContext::Init() {
  if (initialized_ || instance_initialized_ || device_initialized_ || queue_family_initialized_) {
    RTN_MSG(false, "Attempt to re-initialize VulkanContext.\n");
  }
  if (!InitInstance()) {
    RTN_MSG(false, "Unable to initialize instance.\n");
  }
  if (!InitQueueFamily()) {
    RTN_MSG(false, "Unable to initialize queue family.\n");
  }
  if (!InitDevice()) {
    RTN_MSG(false, "Unable to initialize device.\n");
  }
  initialized_ = true;
  return true;
}

bool VulkanContext::set_instance_info(const vk::InstanceCreateInfo &instance_info) {
  if (instance_initialized_) {
    RTN_MSG(false, "set_instance_info ignored.  Instance is already initialized.\n");
  }
  instance_info_ = instance_info;
  return true;
}

bool VulkanContext::set_device_info(const vk::DeviceCreateInfo &device_info) {
  if (device_initialized_) {
    RTN_MSG(false, "set_device_info ignored.  Device is already initialized.\n");
  }
  device_info_ = device_info;
  return true;
}

bool VulkanContext::set_queue_info(const vk::DeviceQueueCreateInfo &queue_info) {
  if (queue_family_initialized_) {
    RTN_MSG(false, "set_queue_info ignored.  Queue family is already initialized.\n");
  }
  queue_info_ = queue_info;
  return true;
}

bool VulkanContext::set_queue_flag_bits(const vk::QueueFlagBits &queue_flag_bits) {
  if (queue_family_initialized_) {
    RTN_MSG(false, "set_queue_flag_bits ignored. Queue family is already initialized.\n");
  }
  queue_flag_bits_ = queue_flag_bits;
  return true;
}

const vk::UniqueInstance &VulkanContext::instance() const {
  if (!instance_initialized_) {
    assert(false && "Instance is not initialized.\n");
  }
  return instance_;
}

const vk::PhysicalDevice &VulkanContext::physical_device() const {
  if (!queue_family_initialized_) {
    assert(false && "Queue family is not initialized.\n");
  }
  return physical_device_;
}

int VulkanContext::queue_family_index() const {
  if (!queue_family_initialized_) {
    assert(false && "Queue family is not initialized.\n");
  }
  return queue_family_index_;
}

const vk::UniqueDevice &VulkanContext::device() const {
  if (!device_initialized_) {
    assert(false && "Device is not initialized.\n");
  }
  return device_;
}

const vk::Queue &VulkanContext::queue() const {
  if (!device_initialized_) {
    assert(false && "Device is not initialized.\n");
  }
  return queue_;
}

VulkanContext::Builder::Builder()
    : physical_device_index_(0),
      queue_priority_(0.0f),
      queue_info_(vk::DeviceQueueCreateFlags(), physical_device_index_, 1 /* queueCount */,
                  &queue_priority_),
      device_info_(vk::DeviceCreateFlags(), 1 /* queueCreateInfoCount */, &queue_info_),
      queue_flag_bits_(vk::QueueFlagBits::eGraphics),
      allocator_(nullptr) {}

VulkanContext::Builder &VulkanContext::Builder::set_allocator(
    vk::Optional<const vk::AllocationCallbacks> allocator) {
  allocator_ = allocator;
  return *this;
}

VulkanContext::Builder &VulkanContext::Builder::set_instance_info(
    const vk::InstanceCreateInfo &instance_info) {
  instance_info_ = instance_info;
  return *this;
}

VulkanContext::Builder &VulkanContext::Builder::set_physical_device_index(
    const size_t physical_device_index) {
  physical_device_index_ = physical_device_index;
  return *this;
}

VulkanContext::Builder &VulkanContext::Builder::set_device_info(
    const vk::DeviceCreateInfo &device_info) {
  device_info_ = device_info;
  return *this;
}

VulkanContext::Builder &VulkanContext::Builder::set_queue_info(
    const vk::DeviceQueueCreateInfo &queue_info) {
  queue_info_ = queue_info;
  return *this;
}

VulkanContext::Builder &VulkanContext::Builder::set_queue_flag_bits(
    const vk::QueueFlagBits &queue_flag_bits) {
  queue_flag_bits_ = queue_flag_bits;
  return *this;
}

std::unique_ptr<VulkanContext> VulkanContext::Builder::Unique() const {
  auto context =
      std::make_unique<VulkanContext>(instance_info_, physical_device_index_, device_info_,
                                      queue_info_, queue_flag_bits_, allocator_);
  if (!context->Init()) {
    RTN_MSG(nullptr, "Failed to initialize VulkanContext.\n")
  }
  return context;
}
