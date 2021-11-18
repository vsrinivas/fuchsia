// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "utils.h"

#include "src/graphics/tests/common/vulkan_context.h"

VKAPI_ATTR VkBool32 VKAPI_CALL DebugUtilsTestCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT msg_severity, VkDebugUtilsMessageTypeFlagsEXT msg_types,
    const VkDebugUtilsMessengerCallbackDataEXT *callback_data, void *user_data) {
  auto context_with_data = reinterpret_cast<VulkanContext::ContextWithUserData *>(user_data);
  if (msg_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
    fprintf(stderr, "%s\n", callback_data->pMessage);
    EXPECT_TRUE(context_with_data->context()->validation_errors_ignored());
  } else {
    fprintf(stdout, "%s\n", callback_data->pMessage);
  }
  return VK_FALSE;
}

VulkanExtensionSupportState GetVulkanTimelineSemaphoreSupport(uint32_t instance_api_version) {
  vk::ApplicationInfo app_info;
  app_info.apiVersion = instance_api_version;

  vk::InstanceCreateInfo instance_info;
  instance_info.pApplicationInfo = &app_info;

  auto [instance_result, instance] = vk::createInstanceUnique(instance_info);
  if (instance_result != vk::Result::eSuccess) {
    RTN_MSG(VulkanExtensionSupportState::kNotSupported, "Failed to create Vulkan instance.\n");
  }

  auto [phy_dev_result, physical_devices] = instance->enumeratePhysicalDevices();
  if (phy_dev_result != vk::Result::eSuccess) {
    RTN_MSG(VulkanExtensionSupportState::kNotSupported, "Failed to get physical devices.\n");
  }

  const uint32_t kDeviceIndex = 0u;
  const auto &physical_device = physical_devices[kDeviceIndex];
  auto device_properties = physical_device.getProperties();

  // We check Vulkan core support only if both the instance version and device
  // API version are no earlier than 1.2.
  if (instance_api_version >= VK_API_VERSION_1_2 &&
      device_properties.apiVersion >= VK_API_VERSION_1_2) {
    auto supported_features =
        physical_device
            .getFeatures2<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan12Features>();
    if (supported_features.get<vk::PhysicalDeviceVulkan12Features>().timelineSemaphore) {
      return VulkanExtensionSupportState::kSupportedInCore;
    }
  }

  // If device / instance API version < 1.2, we should check if the device supports
  // VK_KHR_timeline_semaphore extension.
  auto extension_rv = physical_device.enumerateDeviceExtensionProperties();
  if (extension_rv.result != vk::Result::eSuccess) {
    RTN_MSG(VulkanExtensionSupportState::kNotSupported,
            "Failed to get device extension properties.\n");
  }

  auto extensions = extension_rv.value;
  auto found_ext = std::find_if(extensions.begin(), extensions.end(), [](const auto &extension) {
                     return strcmp(extension.extensionName.data(),
                                   VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME) == 0;
                   }) != extensions.end();

  if (found_ext) {
    auto supported_features =
        physical_device.getFeatures2<vk::PhysicalDeviceFeatures2,
                                     vk::PhysicalDeviceTimelineSemaphoreFeaturesKHR>();
    if (supported_features.get<vk::PhysicalDeviceTimelineSemaphoreFeaturesKHR>()
            .timelineSemaphore) {
      return VulkanExtensionSupportState::kSupportedAsExtensionOnly;
    }
  }
  return VulkanExtensionSupportState::kNotSupported;
}
