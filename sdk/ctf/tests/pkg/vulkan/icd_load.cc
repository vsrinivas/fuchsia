// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdint>
#include <cstdio>
#include <vector>

#include <vulkan/vulkan.h>
#include <zxtest/zxtest.h>

class IcdLoadTest {
 public:
  static void LoadIcd();
};

void IcdLoadTest::LoadIcd() {
  // vkEnumerateInstanceExtensionProperties is the chosen entrypoint because
  // it doesn't require an instance parameter.
  uint32_t num_extensions = 0;
  vkEnumerateInstanceExtensionProperties(nullptr, &num_extensions, nullptr);

  std::vector<VkExtensionProperties> extensions(num_extensions);
  vkEnumerateInstanceExtensionProperties(nullptr, &num_extensions, extensions.data());

  // The VCD must be loaded for VK_KHR_get_physical_device_properties2
  // to be available.
  const char* kRequiredExtension = "VK_KHR_get_physical_device_properties2";
  bool found_extension = false;
  for (const auto& extension : extensions) {
    if (!strcmp(extension.extensionName, kRequiredExtension)) {
      found_extension = true;
      break;
    }
  }
  EXPECT_TRUE(found_extension);
}

TEST(Vulkan, IcdLoad) {
  IcdLoadTest test;
  test.LoadIcd();
}

TEST(Vulkan, CreateInstance) {
  VkInstanceCreateInfo info{.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
  VkInstance instance;
  ASSERT_EQ(VK_SUCCESS, vkCreateInstance(&info, nullptr, &instance));

  VkPhysicalDevice device;
  uint32_t physicalDeviceCount = 1;
  VkResult result = vkEnumeratePhysicalDevices(instance, &physicalDeviceCount, &device);
  EXPECT_TRUE(result == VK_SUCCESS || result == VK_INCOMPLETE, "result %d", result);
  EXPECT_EQ(1u, physicalDeviceCount);
  vkDestroyInstance(instance, nullptr);
}
