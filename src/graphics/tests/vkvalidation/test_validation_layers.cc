// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>
#include <vulkan/vulkan.h>

TEST(ValidationLayers, InstanceLayers) {
  uint32_t layer_count;
  EXPECT_EQ(VK_SUCCESS, vkEnumerateInstanceLayerProperties(&layer_count, NULL));
  ASSERT_GE(layer_count, 1u);

  std::vector<VkLayerProperties> layers(layer_count);
  EXPECT_EQ(VK_SUCCESS, vkEnumerateInstanceLayerProperties(&layer_count, layers.data()));

  bool found_std_validation = false;
  bool found_khronos_validation = false;

  for (auto& layer : layers) {
    if (strcmp(layer.layerName, "VK_LAYER_LUNARG_standard_validation") == 0)
      found_std_validation = true;
    else if (strcmp(layer.layerName, "VK_LAYER_KHRONOS_validation") == 0)
      found_khronos_validation = true;
  }

  EXPECT_TRUE(found_std_validation);
  EXPECT_TRUE(found_khronos_validation);
}

namespace {

VkBool32 debugMessageCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
                              VkDebugUtilsMessageTypeFlagsEXT messageTypes,
                              const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
                              void* pUserData) {
  if (messageTypes & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT) {
    auto validation_error_count = reinterpret_cast<uint32_t*>(pUserData);
    *validation_error_count += 1;
  }
  return VK_FALSE;
}

void test_validation_layer(const char* layer_name) {
  uint32_t instance_extension_count;
  EXPECT_EQ(VK_SUCCESS,
            vkEnumerateInstanceExtensionProperties(nullptr, &instance_extension_count, nullptr));
  ASSERT_GE(instance_extension_count, 1u);

  std::vector<VkExtensionProperties> instance_extensions(instance_extension_count);
  EXPECT_EQ(VK_SUCCESS, vkEnumerateInstanceExtensionProperties(nullptr, &instance_extension_count,
                                                               instance_extensions.data()));

  bool found_debug_ext = false;
  for (auto& extension : instance_extensions) {
    if (strcmp(VK_EXT_DEBUG_UTILS_EXTENSION_NAME, extension.extensionName) == 0) {
      found_debug_ext = true;
    }
  }
  ASSERT_TRUE(found_debug_ext);

  uint32_t validation_error_count = 0;

  VkDebugUtilsMessengerCreateInfoEXT debug_create_info = {
      .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
      .pNext = nullptr,
      .flags = 0,
      .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                         VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
                         VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                         VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
      .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                     VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                     VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
      .pfnUserCallback = debugMessageCallback,
      .pUserData = &validation_error_count,
  };

  struct VkApplicationInfo app_info = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                                       .pNext = nullptr,
                                       .apiVersion = VK_API_VERSION_1_1};

  std::vector<const char*> layerNameArray{layer_name};
  std::vector<const char*> instanceExtensionArray{VK_EXT_DEBUG_UTILS_EXTENSION_NAME};

  VkInstanceCreateInfo inst_info = {
      .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
      .pNext = &debug_create_info,
      .pApplicationInfo = &app_info,
      .enabledLayerCount = static_cast<uint32_t>(layerNameArray.size()),
      .ppEnabledLayerNames = layerNameArray.data(),
      .enabledExtensionCount = static_cast<uint32_t>(instanceExtensionArray.size()),
      .ppEnabledExtensionNames = instanceExtensionArray.data(),
  };

  VkInstance vk_instance;
  ASSERT_EQ(VK_SUCCESS, vkCreateInstance(&inst_info, nullptr, &vk_instance));

  auto f_vkCreateDebugUtilsMessengerEXT = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
      vkGetInstanceProcAddr(vk_instance, "vkCreateDebugUtilsMessengerEXT"));
  ASSERT_TRUE(f_vkCreateDebugUtilsMessengerEXT);

  VkDebugUtilsMessengerEXT messenger;
  ASSERT_EQ(VK_SUCCESS,
            f_vkCreateDebugUtilsMessengerEXT(vk_instance, &debug_create_info, nullptr, &messenger));

  uint32_t phys_device_count = 1;
  VkPhysicalDevice phys_device = VK_NULL_HANDLE;
  VkResult result = vkEnumeratePhysicalDevices(vk_instance, &phys_device_count, &phys_device);
  ASSERT_TRUE(result == VK_SUCCESS || result == VK_INCOMPLETE);
  if (result == VK_INCOMPLETE) {
    printf("vkEnumeratePhysicalDevices returned VK_INCOMPLETE: phys_device_count %u\n",
           phys_device_count);
  }

  uint32_t queue_family_count;
  vkGetPhysicalDeviceQueueFamilyProperties(phys_device, &queue_family_count, nullptr);
  ASSERT_GE(queue_family_count, 1u);

  std::vector<VkQueueFamilyProperties> queue_family_properties(queue_family_count);
  vkGetPhysicalDeviceQueueFamilyProperties(phys_device, &queue_family_count,
                                           queue_family_properties.data());

  int32_t queue_family_index = -1;
  for (uint32_t i = 0; i < queue_family_count; i++) {
    if (queue_family_properties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
      queue_family_index = i;
      break;
    }
  }
  ASSERT_GE(queue_family_index, 0);

  float queue_priorities[]{0.0f};
  VkDeviceQueueCreateInfo queue_create_info = {
      .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
      .pNext = nullptr,
      .flags = 0,
      .queueFamilyIndex = static_cast<uint32_t>(queue_family_index),
      .queueCount = 1,
      .pQueuePriorities = queue_priorities,
  };
  VkDeviceCreateInfo device_create_info = {
      .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
      .pNext = nullptr,
      .queueCreateInfoCount = 1,
      .pQueueCreateInfos = &queue_create_info,
  };
  VkDevice vk_device;
  ASSERT_EQ(VK_SUCCESS, vkCreateDevice(phys_device, &device_create_info, nullptr, &vk_device));

  VkQueue vk_queue;
  // vkGetDeviceQeueue should trigger a validation error given VK_QUEUE_FAMILY_IGNORED
  vkGetDeviceQueue(vk_device, VK_QUEUE_FAMILY_IGNORED, 0, &vk_queue);

  EXPECT_GE(validation_error_count, 1u);

  vkDestroyDevice(vk_device, nullptr);
  vkDestroyInstance(vk_instance, nullptr);
}

}  // namespace

TEST(ValidationLayers, StandardValidation) {
  test_validation_layer("VK_LAYER_LUNARG_standard_validation");
}

TEST(ValidationLayers, KhronosValidation) { test_validation_layer("VK_LAYER_KHRONOS_validation"); }
