// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <vulkan/vulkan.h>
#include "gtest/gtest.h"

static const char* kLayerName = "VK_LAYER_GOOGLE_image_pipe_swapchain";

// Note: the loader returns results based on the layer's manifest file, not the
// implementation of the vkEnumerateInstanceExtensionProperties and
// vkEnumerateDeviceExtensionProperties apis inside the layer.

const char* expected_instance_extensions[] = {
    VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_MAGMA_SURFACE_EXTENSION_NAME,
    "foobar"};

const char* expected_device_extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME,
                                            "bluecheese"};

#define ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))

TEST(Swapchain, InstanceExtensions) {
  uint32_t prop_count = 0;
  EXPECT_EQ(VK_SUCCESS, vkEnumerateInstanceExtensionProperties(
                            kLayerName, &prop_count, nullptr));
  EXPECT_EQ(prop_count, ARRAY_SIZE(expected_instance_extensions));

  std::vector<VkExtensionProperties> props(prop_count);
  EXPECT_EQ(VK_SUCCESS, vkEnumerateInstanceExtensionProperties(
                            kLayerName, &prop_count, props.data()));
  for (uint32_t i = 0; i < prop_count; i++) {
    EXPECT_STREQ(expected_instance_extensions[i], props[i].extensionName);
    props[i].extensionName[0] = 0;
  }

  prop_count = 0;
  EXPECT_EQ(VK_SUCCESS, vkEnumerateInstanceExtensionProperties(
                            nullptr, &prop_count, nullptr));
  props.resize(prop_count);

  EXPECT_EQ(VK_SUCCESS, vkEnumerateInstanceExtensionProperties(
                            nullptr, &prop_count, props.data()));

  uint32_t found_count = 0;
  for (uint32_t i = 0; i < ARRAY_SIZE(expected_instance_extensions); i++) {
    for (uint32_t j = 0; j < props.size(); j++) {
      if (strcmp(expected_instance_extensions[i], props[j].extensionName) == 0)
        found_count++;
    }
  }
  EXPECT_EQ(found_count, ARRAY_SIZE(expected_instance_extensions));

  // Without this loader will only allow extensions that it knows about.
  char envstr[] = "VK_LOADER_DISABLE_INST_EXT_FILTER=1";
  putenv(envstr);

  VkInstanceCreateInfo inst_info = {
      .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
      .pNext = nullptr,
      .pApplicationInfo = nullptr,
      .enabledLayerCount = 0,
      .ppEnabledLayerNames = nullptr,
      .enabledExtensionCount = ARRAY_SIZE(expected_instance_extensions),
      .ppEnabledExtensionNames = expected_instance_extensions,
  };
  VkInstance instance;
  ASSERT_EQ(VK_SUCCESS, vkCreateInstance(&inst_info, nullptr, &instance));
}

TEST(Swapchain, DeviceExtensions) {
  VkInstanceCreateInfo inst_info = {
      .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
      .pNext = nullptr,
      .pApplicationInfo = nullptr,
      .enabledLayerCount = 0,
      .ppEnabledLayerNames = nullptr,
      .enabledExtensionCount = 0,
      .ppEnabledExtensionNames = nullptr,
  };
  VkInstance instance;

  ASSERT_EQ(VK_SUCCESS, vkCreateInstance(&inst_info, nullptr, &instance));

  uint32_t gpu_count;
  ASSERT_EQ(VK_SUCCESS,
            vkEnumeratePhysicalDevices(instance, &gpu_count, nullptr));
  EXPECT_GE(gpu_count, 1u);

  std::vector<VkPhysicalDevice> physical_devices(gpu_count);
  ASSERT_EQ(VK_SUCCESS, vkEnumeratePhysicalDevices(instance, &gpu_count,
                                                   physical_devices.data()));

  uint32_t prop_count;
  EXPECT_EQ(VK_SUCCESS,
            vkEnumerateDeviceExtensionProperties(
                physical_devices[0], kLayerName, &prop_count, nullptr));
  EXPECT_EQ(prop_count, ARRAY_SIZE(expected_device_extensions));

  std::vector<VkExtensionProperties> props(prop_count);
  EXPECT_EQ(VK_SUCCESS,
            vkEnumerateDeviceExtensionProperties(
                physical_devices[0], kLayerName, &prop_count, props.data()));
  for (uint32_t i = 0; i < prop_count; i++) {
    EXPECT_STREQ(expected_device_extensions[i], props[i].extensionName);
  }

  float queue_priorities[1] = {0.0};
  VkDeviceQueueCreateInfo queue_create_info = {
      .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
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
      .enabledExtensionCount = ARRAY_SIZE(expected_device_extensions),
      .ppEnabledExtensionNames = expected_device_extensions,
      .pEnabledFeatures = nullptr};
  VkDevice device;
  EXPECT_EQ(VK_SUCCESS, vkCreateDevice(physical_devices[0], &device_create_info,
                                       nullptr, &device));
}
