// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/examples/vkprimer/common/physical_device.h"

#include "src/graphics/examples/vkprimer/common/swapchain.h"
#include "src/graphics/examples/vkprimer/common/utils.h"

#include <vulkan/vulkan.hpp>

namespace {

const char *kMagmaLayer = "VK_LAYER_FUCHSIA_imagepipe_swapchain_fb";

const std::vector<const char *> s_required_physical_device_props = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
#ifdef __Fuchsia__
    VK_FUCHSIA_EXTERNAL_MEMORY_EXTENSION_NAME,
    VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME,
    VK_FUCHSIA_EXTERNAL_SEMAPHORE_EXTENSION_NAME,
    VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME,
#endif
};

bool ChooseGraphicsDevice(const vk::PhysicalDevice &physical_device_in, const VkSurfaceKHR &surface,
                          vk::PhysicalDevice *physical_device_out) {
  if (!FindRequiredProperties(s_required_physical_device_props, vkp::PHYS_DEVICE_EXT_PROP,
                              kMagmaLayer, physical_device_in)) {
    return false;
  }

  if (surface) {
    vkp::Swapchain::Info swapchain_info;
    if (!vkp::Swapchain::QuerySwapchainSupport(physical_device_in, surface, &swapchain_info)) {
      return false;
    }
  }

  if (!vkp::FindGraphicsQueueFamilyIndex(physical_device_in, surface)) {
    RTN_MSG(false, "No graphics queue families found%s.\n", surface ? " with present support" : "");
  }
  *physical_device_out = physical_device_in;
  return true;
}

}  // namespace

namespace vkp {

PhysicalDevice::PhysicalDevice(std::shared_ptr<vk::Instance> instance, VkSurfaceKHR surface)
    : initialized_(false), instance_(instance), surface_(surface) {}

bool PhysicalDevice::Init() {
  RTN_IF_MSG(false, initialized_, "PhysicalDevice already initialized.\n");
  RTN_IF_MSG(false, !instance_, "Instance must be initialized.\n");

  auto [r_phys_devices, phys_devices] = instance_->enumeratePhysicalDevices();
  if (vk::Result::eSuccess != r_phys_devices || phys_devices.empty()) {
    RTN_MSG(false, "VK Error: 0x%x - No physical device found.\n", r_phys_devices);
  }

  for (const auto &phys_device : phys_devices) {
    if (ChooseGraphicsDevice(phys_device, surface_, &physical_device_)) {
      LogMemoryProperties(physical_device_);
      initialized_ = true;
      break;
    }
  }

  RTN_IF_MSG(false, !initialized_, "Couldn't find graphics family device.\n");
  return initialized_;
}

void PhysicalDevice::AppendRequiredPhysDeviceExts(std::vector<const char *> *exts) {
  exts->insert(exts->end(), s_required_physical_device_props.begin(),
               s_required_physical_device_props.end());
}

const vk::PhysicalDevice &PhysicalDevice::get() const {
  if (!initialized_) {
    RTN_MSG(physical_device_, "%s", "Request for uninitialized instance.\n");
  }
  return physical_device_;
}

}  // namespace vkp
