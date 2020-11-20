// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/examples/vkprimer/common/physical_device.h"

#include "src/graphics/examples/vkprimer/common/swapchain.h"
#include "src/graphics/examples/vkprimer/common/utils.h"

namespace {

const char *kMagmaLayer = "VK_LAYER_FUCHSIA_imagepipe_swapchain_fb";

const std::vector<const char *> s_required_phys_device_props = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
#ifdef __Fuchsia__
    VK_FUCHSIA_EXTERNAL_MEMORY_EXTENSION_NAME,
    VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME,
    VK_FUCHSIA_EXTERNAL_SEMAPHORE_EXTENSION_NAME,
    VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME,
#endif
};

bool ChooseGraphicsDevice(const vk::PhysicalDevice &phys_device, const VkSurfaceKHR &surface,
                          vk::PhysicalDevice *phys_device_out) {
  if (!FindRequiredProperties(s_required_phys_device_props, vkp::PHYS_DEVICE_EXT_PROP, phys_device,
                              kMagmaLayer, nullptr /* missing_props */)) {
    return false;
  }

  vkp::Swapchain::Info swapchain_info;
  if (!vkp::Swapchain::QuerySwapchainSupport(phys_device, surface, &swapchain_info)) {
    return false;
  }

  if (!vkp::FindGraphicsQueueFamilies(phys_device, surface, nullptr)) {
    RTN_MSG(false, "No graphics queue families.\n");
  }
  *phys_device_out = phys_device;
  return true;
}

}  // namespace

namespace vkp {

PhysicalDevice::PhysicalDevice(std::shared_ptr<Instance> vkp_instance, const VkSurfaceKHR &surface)
    : initialized_(false), vkp_instance_(std::move(vkp_instance)) {
  params_ = std::make_unique<InitParams>(surface);
}

PhysicalDevice::InitParams::InitParams(const VkSurfaceKHR &surface) : surface_(surface) {}

bool PhysicalDevice::Init() {
  if (initialized_) {
    RTN_MSG(false, "PhysicalDevice already initialized.\n");
  }

  auto [result, phys_devices] = vkp_instance_->get().enumeratePhysicalDevices();
  if (vk::Result::eSuccess != result || phys_devices.empty()) {
    RTN_MSG(false, "VK Error: 0x%x - No physical device found.\n", result);
  }

  for (const auto &phys_device : phys_devices) {
    if (ChooseGraphicsDevice(phys_device, params_->surface_, &phys_device_)) {
      LogMemoryProperties(phys_device_);
      initialized_ = true;
      break;
    }
  }

  if (!initialized_) {
    RTN_MSG(false, "Couldn't find graphics family device.\n");
  }
  params_.reset();
  return initialized_;
}

void PhysicalDevice::AppendRequiredPhysDeviceExts(std::vector<const char *> *exts) {
  exts->insert(exts->end(), s_required_phys_device_props.begin(),
               s_required_phys_device_props.end());
}

const vk::PhysicalDevice &PhysicalDevice::get() const {
  if (!initialized_) {
    RTN_MSG(phys_device_, "%s", "Request for uninitialized instance.\n");
  }
  return phys_device_;
}

}  // namespace vkp
