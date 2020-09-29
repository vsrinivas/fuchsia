// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/util/check_vulkan_support.h"

#include <vulkan/vulkan.hpp>

namespace escher {

namespace {

vk::Instance CreateVulkanInstance() {
  vk::InstanceCreateInfo instance_info;
  instance_info.enabledLayerCount = 0;
  instance_info.ppEnabledLayerNames = nullptr;
  instance_info.enabledExtensionCount = 0;
  instance_info.ppEnabledExtensionNames = nullptr;

  vk::Instance instance;
  if (vk::Result::eSuccess == vk::createInstance(&instance_info, nullptr, &instance)) {
    return instance;
  }
  return vk::Instance();
}

vk::Device CreateVulkanDevice(vk::Instance instance) {
  constexpr uint32_t kMaxPhysicalDevices = 100;
  vk::PhysicalDevice physical_devices[kMaxPhysicalDevices];
  uint32_t num_physical_devices = kMaxPhysicalDevices;
  if (vk::Result::eSuccess !=
      instance.enumeratePhysicalDevices(&num_physical_devices, physical_devices)) {
    return vk::Device();
  }

  const auto kRequiredQueueFlags =
      vk::QueueFlagBits::eTransfer | vk::QueueFlagBits::eGraphics | vk::QueueFlagBits::eCompute;
  for (uint32_t i = 0; i < num_physical_devices; ++i) {
    auto& physical_device = physical_devices[i];
    auto queues = physical_device.getQueueFamilyProperties();
    for (uint32_t i = 0; i < queues.size(); ++i) {
      if (kRequiredQueueFlags == (queues[i].queueFlags & kRequiredQueueFlags)) {
        vk::DeviceQueueCreateInfo queue_info;
        const float kQueuePriority = 0;
        queue_info.queueFamilyIndex = i;
        queue_info.queueCount = 1;
        queue_info.pQueuePriorities = &kQueuePriority;

        vk::DeviceCreateInfo device_info;
        device_info.queueCreateInfoCount = 1;
        device_info.pQueueCreateInfos = &queue_info;
        device_info.enabledExtensionCount = 0;
        device_info.ppEnabledExtensionNames = nullptr;

        vk::Device device;
        if (vk::Result::eSuccess == physical_device.createDevice(&device_info, nullptr, &device)) {
          return device;
        }
      }
    }
  }

  return vk::Device();
}

bool CheckIfVulkanIsSupported() {
  vk::Instance instance = CreateVulkanInstance();
  if (!instance) {
    return false;
  }

  vk::Device device = CreateVulkanDevice(instance);
  if (!device) {
    instance.destroy();
    return false;
  }

  device.destroy();
  instance.destroy();
  return true;
}

}  // anonymous namespace

bool VulkanIsSupported() {
  static bool is_supported = CheckIfVulkanIsSupported();
  return is_supported;
}

}  // namespace escher
