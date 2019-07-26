// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <vulkan/vulkan.h>
#include "gtest/gtest.h"

static const char* kLayerName = "VK_LAYER_FUCHSIA_imagepipe_swapchain";

// Note: the loader returns results based on the layer's manifest file, not the
// implementation of the vkEnumerateInstanceExtensionProperties and
// vkEnumerateDeviceExtensionProperties apis inside the layer.

static const std::vector<const char*> kLayers = {kLayerName};

static const std::vector<const char*> kExpectedInstanceExtensions = {
    VK_KHR_SURFACE_EXTENSION_NAME, VK_FUCHSIA_IMAGEPIPE_SURFACE_EXTENSION_NAME};

static const std::vector<const char*> kExpectedDeviceExtensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

TEST(Swapchain, LayerApiVersion) {
  uint32_t prop_count = 0;
  EXPECT_EQ(VK_SUCCESS, vkEnumerateInstanceLayerProperties(&prop_count, nullptr));
  EXPECT_GE(prop_count, kLayers.size());

  std::vector<VkLayerProperties> props(prop_count);
  EXPECT_EQ(VK_SUCCESS, vkEnumerateInstanceLayerProperties(&prop_count, props.data()));
  bool layer_found = false;
  const uint32_t kExpectedVersion = VK_MAKE_VERSION(1, 1, VK_HEADER_VERSION);
  for (uint32_t i = 0; i < prop_count; i++) {
    if (strcmp(props[i].layerName, kLayerName) == 0) {
      EXPECT_GE(kExpectedVersion, props[i].specVersion);
      layer_found = true;
      break;
    }
  }
  EXPECT_TRUE(layer_found);
}

TEST(Swapchain, InstanceExtensions) {
  uint32_t prop_count = 0;
  EXPECT_EQ(VK_SUCCESS, vkEnumerateInstanceExtensionProperties(kLayerName, &prop_count, nullptr));
  EXPECT_EQ(prop_count, kExpectedInstanceExtensions.size());

  std::vector<VkExtensionProperties> props(prop_count);
  EXPECT_EQ(VK_SUCCESS,
            vkEnumerateInstanceExtensionProperties(kLayerName, &prop_count, props.data()));
  for (uint32_t i = 0; i < prop_count; i++) {
    EXPECT_STREQ(kExpectedInstanceExtensions[i], props[i].extensionName);
  }

  VkInstanceCreateInfo inst_info = {
      .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
      .pNext = nullptr,
      .pApplicationInfo = nullptr,
      .enabledLayerCount = static_cast<uint32_t>(kLayers.size()),
      .ppEnabledLayerNames = kLayers.data(),
      .enabledExtensionCount = static_cast<uint32_t>(kExpectedInstanceExtensions.size()),
      .ppEnabledExtensionNames = kExpectedInstanceExtensions.data(),
  };
  VkInstance instance;
  ASSERT_EQ(VK_SUCCESS, vkCreateInstance(&inst_info, nullptr, &instance));
}

TEST(Swapchain, DeviceExtensions) {
  VkInstanceCreateInfo inst_info = {
      .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
      .pNext = nullptr,
      .pApplicationInfo = nullptr,
      .enabledLayerCount = static_cast<uint32_t>(kLayers.size()),
      .ppEnabledLayerNames = kLayers.data(),
      .enabledExtensionCount = static_cast<uint32_t>(kExpectedInstanceExtensions.size()),
      .ppEnabledExtensionNames = kExpectedInstanceExtensions.data(),
  };
  VkInstance instance;

  ASSERT_EQ(VK_SUCCESS, vkCreateInstance(&inst_info, nullptr, &instance));

  uint32_t gpu_count;
  ASSERT_EQ(VK_SUCCESS, vkEnumeratePhysicalDevices(instance, &gpu_count, nullptr));
  EXPECT_GE(gpu_count, 1u);

  std::vector<VkPhysicalDevice> physical_devices(gpu_count);
  ASSERT_EQ(VK_SUCCESS, vkEnumeratePhysicalDevices(instance, &gpu_count, physical_devices.data()));

  uint32_t prop_count;
  EXPECT_EQ(VK_SUCCESS, vkEnumerateDeviceExtensionProperties(physical_devices[0], kLayerName,
                                                             &prop_count, nullptr));
  EXPECT_EQ(prop_count, kExpectedDeviceExtensions.size());

  std::vector<VkExtensionProperties> props(prop_count);
  EXPECT_EQ(VK_SUCCESS, vkEnumerateDeviceExtensionProperties(physical_devices[0], kLayerName,
                                                             &prop_count, props.data()));
  for (uint32_t i = 0; i < prop_count; i++) {
    EXPECT_STREQ(kExpectedDeviceExtensions[i], props[i].extensionName);
  }

  float queue_priorities[1] = {0.0};
  VkDeviceQueueCreateInfo queue_create_info = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                                               .pNext = nullptr,
                                               .queueFamilyIndex = 0,
                                               .queueCount = 1,
                                               .pQueuePriorities = queue_priorities,
                                               .flags = 0};
  VkDeviceCreateInfo device_create_info = {
      .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
      .pNext = nullptr,
      .queueCreateInfoCount = 1,
      .pQueueCreateInfos = &queue_create_info,
      .enabledLayerCount = 0,
      .ppEnabledLayerNames = nullptr,
      .enabledExtensionCount = static_cast<uint32_t>(kExpectedDeviceExtensions.size()),
      .ppEnabledExtensionNames = kExpectedDeviceExtensions.data(),
      .pEnabledFeatures = nullptr};
  VkDevice device;
  EXPECT_EQ(VK_SUCCESS, vkCreateDevice(physical_devices[0], &device_create_info, nullptr, &device));
}
