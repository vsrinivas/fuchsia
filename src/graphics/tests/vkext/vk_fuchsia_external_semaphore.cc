// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <array>
#include <thread>
#include <vector>

#include <vulkan/vulkan.h>

#include "gtest/gtest.h"
#include "platform_semaphore.h"

#define PRINT_STDERR(format, ...) \
  fprintf(stderr, "%s:%d " format "\n", __FILE__, __LINE__, ##__VA_ARGS__)

namespace {

class VulkanTest {
 public:
  bool Initialize();
  static bool Exec(VulkanTest* t1, VulkanTest* t2, bool temporary);
  static bool ExecUsingQueue(VulkanTest* t1, VulkanTest* t2, bool temporary);

 private:
  bool InitVulkan();
  bool InitImage();

  bool is_initialized_ = false;
  PFN_vkGetPhysicalDeviceExternalSemaphorePropertiesKHR
      vkGetPhysicalDeviceExternalSemaphorePropertiesKHR_;
  PFN_vkImportSemaphoreZirconHandleFUCHSIA vkImportSemaphoreZirconHandleFUCHSIA_;
  PFN_vkGetSemaphoreZirconHandleFUCHSIA vkGetSemaphoreZirconHandleFUCHSIA_;

  VkPhysicalDevice vk_physical_device_;
  VkDevice vk_device_;
  VkQueue vk_queue_;

  static constexpr uint32_t kSemaphoreCount = 2;
  std::vector<VkSemaphore> vk_semaphore_;
};

bool VulkanTest::Initialize() {
  if (is_initialized_)
    return false;

  if (!InitVulkan()) {
    PRINT_STDERR("failed to initialize Vulkan");
    return false;
  }

  is_initialized_ = true;

  return true;
}

bool VulkanTest::InitVulkan() {
  VkResult result;

  uint32_t extension_count;
  result = vkEnumerateInstanceExtensionProperties(nullptr, &extension_count, nullptr);
  if (result != VK_SUCCESS) {
    PRINT_STDERR("vkEnumerateInstanceExtensionProperties returned %d", result);
    return false;
  }

  std::vector<VkExtensionProperties> extension_properties(extension_count);
  result = vkEnumerateInstanceExtensionProperties(nullptr, &extension_count,
                                                  extension_properties.data());
  if (result != VK_SUCCESS) {
    PRINT_STDERR("vkEnumerateInstanceExtensionProperties returned %d", result);
    return false;
  }

  std::array<const char*, 2> instance_extensions{
      VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME,
      VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME};
  std::array<const char*, 2> device_extensions{VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME,
                                               VK_FUCHSIA_EXTERNAL_SEMAPHORE_EXTENSION_NAME};

  uint32_t found_count = 0;
  for (auto& prop : extension_properties) {
    for (uint32_t i = 0; i < instance_extensions.size(); i++) {
      if ((strcmp(prop.extensionName, instance_extensions[i]) == 0))
        found_count++;
    }
  }

  if (found_count != instance_extensions.size()) {
    PRINT_STDERR("failed to find instance extensions");
    return false;
  }

  // Setup validation layer.
  uint32_t layer_count;
  result = vkEnumerateInstanceLayerProperties(&layer_count, nullptr);
  if (result != VK_SUCCESS) {
    PRINT_STDERR("vkEnumerateInstanceLayerProperties returned %d", result);
    return false;
  }

  std::vector<VkLayerProperties> layer_properties(layer_count);
  result = vkEnumerateInstanceLayerProperties(&layer_count, layer_properties.data());

  bool found_khr_validation = false;
  bool found_lunarg_validation = false;

  for (const auto& property : layer_properties) {
    found_khr_validation =
        found_khr_validation || (strcmp(property.layerName, "VK_LAYER_KHRONOS_validation") == 0);
    found_lunarg_validation =
        found_lunarg_validation ||
        (strcmp(property.layerName, "VK_LAYER_LUNARG_standard_validation") == 0);
  }

  std::vector<const char*> layers;
  // Vulkan loader is needed for loading layers.
  if (found_khr_validation) {
    layers.push_back("VK_LAYER_KHRONOS_validation");
  } else if (found_lunarg_validation) {
    layers.push_back("VK_LAYER_LUNARG_standard_validation");
  }

  VkInstanceCreateInfo create_info{
      VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,  // VkStructureType             sType;
      nullptr,                                 // const void*                 pNext;
      0,                                       // VkInstanceCreateFlags       flags;
      nullptr,                                 // const VkApplicationInfo*    pApplicationInfo;
      static_cast<uint32_t>(layers.size()),    // uint32_t                    enabledLayerCount;
      layers.data(),                           // const char* const*          ppEnabledLayerNames;
      static_cast<uint32_t>(instance_extensions.size()),
      instance_extensions.data(),
  };
  VkAllocationCallbacks* allocation_callbacks = nullptr;
  VkInstance instance;

  if ((result = vkCreateInstance(&create_info, allocation_callbacks, &instance)) != VK_SUCCESS) {
    PRINT_STDERR("vkCreateInstance failed %d", result);
    return false;
  }

  uint32_t physical_device_count = 0;
  if ((result = vkEnumeratePhysicalDevices(instance, &physical_device_count, nullptr)) !=
      VK_SUCCESS) {
    PRINT_STDERR("vkEnumeratePhysicalDevices failed %d", result);
    return false;
  }

  if (physical_device_count < 1) {
    PRINT_STDERR("unexpected physical_device_count %d", physical_device_count);
    return false;
  }

  std::vector<VkPhysicalDevice> physical_devices(physical_device_count);
  if ((result = vkEnumeratePhysicalDevices(instance, &physical_device_count,
                                           physical_devices.data())) != VK_SUCCESS) {
    PRINT_STDERR("vkEnumeratePhysicalDevices failed %d", result);
    return false;
  }

  uint32_t queue_family_count = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(physical_devices[0], &queue_family_count, nullptr);

  if (queue_family_count < 1) {
    PRINT_STDERR("invalid queue_family_count %d", queue_family_count);
    return false;
  }

  std::vector<VkQueueFamilyProperties> queue_family_properties(queue_family_count);
  vkGetPhysicalDeviceQueueFamilyProperties(physical_devices[0], &queue_family_count,
                                           queue_family_properties.data());

  int32_t queue_family_index = -1;
  for (uint32_t i = 0; i < queue_family_count; i++) {
    if (queue_family_properties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
      queue_family_index = i;
      break;
    }
  }

  if (queue_family_index < 0) {
    PRINT_STDERR("couldn't find an appropriate queue");
    return false;
  }

  result =
      vkEnumerateDeviceExtensionProperties(physical_devices[0], nullptr, &extension_count, nullptr);
  if (result != VK_SUCCESS) {
    PRINT_STDERR("vkEnumerateDeviceExtensionProperties returned %d", result);
    return false;
  }

  extension_properties.resize(extension_count);
  result = vkEnumerateDeviceExtensionProperties(physical_devices[0], nullptr, &extension_count,
                                                extension_properties.data());
  if (result != VK_SUCCESS) {
    PRINT_STDERR("vkEnumerateDeviceExtensionProperties returned %d", result);
    return false;
  }

  found_count = 0;
  for (const auto& prop : extension_properties) {
    for (uint32_t i = 0; i < device_extensions.size(); i++) {
      if ((strcmp(prop.extensionName, device_extensions[i]) == 0))
        found_count++;
    }
  }

  if (found_count != device_extensions.size()) {
    PRINT_STDERR("failed to find device extensions");
    return false;
  }

  // Create the device
  float queue_priorities[1] = {0.0};

  VkDeviceQueueCreateInfo queue_create_info = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                                               .pNext = nullptr,
                                               .flags = 0,
                                               .queueFamilyIndex = 0,
                                               .queueCount = 1,
                                               .pQueuePriorities = queue_priorities};
  VkDeviceCreateInfo createInfo = {
      .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
      .pNext = nullptr,
      .flags = 0,
      .queueCreateInfoCount = 1,
      .pQueueCreateInfos = &queue_create_info,
      .enabledLayerCount = 0,
      .ppEnabledLayerNames = nullptr,
      .enabledExtensionCount = static_cast<uint32_t>(device_extensions.size()),
      .ppEnabledExtensionNames = device_extensions.data(),
      .pEnabledFeatures = nullptr};
  VkDevice vkdevice;

  if ((result = vkCreateDevice(physical_devices[0], &createInfo, nullptr /* allocationcallbacks */,
                               &vkdevice)) != VK_SUCCESS) {
    PRINT_STDERR("vkCreateDevice failed: %d", result);
    return false;
  }

  vk_physical_device_ = physical_devices[0];
  vk_device_ = vkdevice;

  vkGetDeviceQueue(vkdevice, queue_family_index, 0, &vk_queue_);

  // Get extension function pointers
  vkGetPhysicalDeviceExternalSemaphorePropertiesKHR_ =
      reinterpret_cast<PFN_vkGetPhysicalDeviceExternalSemaphorePropertiesKHR>(
          vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceExternalSemaphorePropertiesKHR"));
  if (!vkGetPhysicalDeviceExternalSemaphorePropertiesKHR_) {
    PRINT_STDERR("couldn't find vkGetPhysicalDeviceExternalSemaphorePropertiesKHR");
    return false;
  }

  vkImportSemaphoreZirconHandleFUCHSIA_ =
      reinterpret_cast<PFN_vkImportSemaphoreZirconHandleFUCHSIA>(
          vkGetDeviceProcAddr(vk_device_, "vkImportSemaphoreZirconHandleFUCHSIA"));
  if (!vkImportSemaphoreZirconHandleFUCHSIA_) {
    PRINT_STDERR("couldn't find vkImportSemaphoreZirconHandleFUCHSIA");
    return false;
  }

  vkGetSemaphoreZirconHandleFUCHSIA_ = reinterpret_cast<PFN_vkGetSemaphoreZirconHandleFUCHSIA>(
      vkGetDeviceProcAddr(vk_device_, "vkGetSemaphoreZirconHandleFUCHSIA"));
  if (!vkGetSemaphoreZirconHandleFUCHSIA_) {
    PRINT_STDERR("couldn't find vkGetSemaphoreZirconHandleFUCHSIA");
    return false;
  }

  VkExternalSemaphorePropertiesKHR external_semaphore_properties = {
      .sType = VK_STRUCTURE_TYPE_EXTERNAL_SEMAPHORE_PROPERTIES_KHR,
  };
  VkPhysicalDeviceExternalSemaphoreInfo external_semaphore_info = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_SEMAPHORE_INFO_KHR,
      .pNext = nullptr,
      .handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_TEMP_ZIRCON_EVENT_BIT_FUCHSIA,
  };
  vkGetPhysicalDeviceExternalSemaphorePropertiesKHR_(vk_physical_device_, &external_semaphore_info,
                                                     &external_semaphore_properties);

  EXPECT_EQ(external_semaphore_properties.compatibleHandleTypes,
            VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_TEMP_ZIRCON_EVENT_BIT_FUCHSIA);
  EXPECT_EQ(external_semaphore_properties.externalSemaphoreFeatures,
            0u | VK_EXTERNAL_SEMAPHORE_FEATURE_EXPORTABLE_BIT_KHR |
                VK_EXTERNAL_SEMAPHORE_FEATURE_IMPORTABLE_BIT_KHR);

  // Create semaphores for export
  for (uint32_t i = 0; i < kSemaphoreCount; i++) {
    VkExportSemaphoreCreateInfo export_create_info = {
        .sType = VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO_KHR,
        .pNext = nullptr,
        .handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_TEMP_ZIRCON_EVENT_BIT_FUCHSIA,
    };

    VkSemaphoreCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        .pNext = &export_create_info,
        .flags = 0,
    };

    VkSemaphore semaphore;
    result = vkCreateSemaphore(vk_device_, &create_info, nullptr, &semaphore);
    if (result != VK_SUCCESS) {
      PRINT_STDERR("vkCreateSemaphore returned %d", result);
      return false;
    }

    vk_semaphore_.push_back(semaphore);
  }

  return true;
}

bool VulkanTest::Exec(VulkanTest* t1, VulkanTest* t2, bool temporary) {
  VkResult result;

  std::vector<uint32_t> handle(kSemaphoreCount);

  // Export semaphores
  for (uint32_t i = 0; i < kSemaphoreCount; i++) {
    VkSemaphoreGetZirconHandleInfoFUCHSIA info{
        .sType = VK_STRUCTURE_TYPE_TEMP_SEMAPHORE_GET_ZIRCON_HANDLE_INFO_FUCHSIA,
        .pNext = nullptr,
        .semaphore = t1->vk_semaphore_[i],
        .handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_TEMP_ZIRCON_EVENT_BIT_FUCHSIA,
    };
    result = t1->vkGetSemaphoreZirconHandleFUCHSIA_(t1->vk_device_, &info, &handle[i]);
    if (result != VK_SUCCESS) {
      PRINT_STDERR("vkGetSemaphoreZirconHandleFUCHSIA returned %d", result);
      return false;
    }
  }

  std::vector<std::unique_ptr<magma::PlatformSemaphore>> exported;

  // Import semaphores
  for (uint32_t i = 0; i < kSemaphoreCount; i++) {
    uint32_t flags = temporary ? VK_SEMAPHORE_IMPORT_TEMPORARY_BIT_KHR : 0;
    exported.emplace_back(magma::PlatformSemaphore::Import(handle[i]));
    uint32_t import_handle;
    EXPECT_TRUE(exported.back()->duplicate_handle(&import_handle));
    VkImportSemaphoreZirconHandleInfoFUCHSIA import_info = {
        .sType = VK_STRUCTURE_TYPE_TEMP_IMPORT_SEMAPHORE_ZIRCON_HANDLE_INFO_FUCHSIA,
        .pNext = nullptr,
        .flags = flags,
        .semaphore = t2->vk_semaphore_[i],
        .handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_TEMP_ZIRCON_EVENT_BIT_FUCHSIA,
        .handle = import_handle};

    result = t1->vkImportSemaphoreZirconHandleFUCHSIA_(t2->vk_device_, &import_info);
    if (result != VK_SUCCESS) {
      PRINT_STDERR("vkImportSemaphoreZirconHandleFUCHSIA failed: %d", result);
      return false;
    }
  }

  // Test semaphores
  for (uint32_t i = 0; i < kSemaphoreCount; i++) {
    auto& platform_semaphore_export = exported[i];

    // Export the imported semaphores
    VkSemaphoreGetZirconHandleInfoFUCHSIA info{
        .sType = VK_STRUCTURE_TYPE_TEMP_SEMAPHORE_GET_ZIRCON_HANDLE_INFO_FUCHSIA,
        .pNext = nullptr,
        .semaphore = t2->vk_semaphore_[i],
        .handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_TEMP_ZIRCON_EVENT_BIT_FUCHSIA,
    };
    result = t1->vkGetSemaphoreZirconHandleFUCHSIA_(t2->vk_device_, &info, &handle[i]);
    if (result != VK_SUCCESS) {
      PRINT_STDERR("vkGetSemaphoreZirconHandleFUCHSIA_ returned %d", result);
      return false;
    }

    std::shared_ptr<magma::PlatformSemaphore> platform_semaphore_import =
        magma::PlatformSemaphore::Import(handle[i]);

    EXPECT_EQ(platform_semaphore_export->id(), platform_semaphore_import->id());

    platform_semaphore_export->Reset();

    std::thread thread(
        [platform_semaphore_import] { EXPECT_TRUE(platform_semaphore_import->Wait(2000)); });

    platform_semaphore_export->Signal();
    thread.join();
  }

  // Destroy semaphores
  for (uint32_t i = 0; i < kSemaphoreCount; i++) {
    vkDestroySemaphore(t1->vk_device_, t1->vk_semaphore_[i], nullptr);
    vkDestroySemaphore(t2->vk_device_, t2->vk_semaphore_[i], nullptr);
  }

  return true;
}

bool VulkanTest::ExecUsingQueue(VulkanTest* t1, VulkanTest* t2, bool temporary) {
  VkResult result;

  std::vector<uint32_t> handle(kSemaphoreCount);

  // Export semaphores
  for (uint32_t i = 0; i < kSemaphoreCount; i++) {
    VkSemaphoreGetZirconHandleInfoFUCHSIA info{
        .sType = VK_STRUCTURE_TYPE_TEMP_SEMAPHORE_GET_ZIRCON_HANDLE_INFO_FUCHSIA,
        .pNext = nullptr,
        .semaphore = t1->vk_semaphore_[i],
        .handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_TEMP_ZIRCON_EVENT_BIT_FUCHSIA,
    };
    result = t1->vkGetSemaphoreZirconHandleFUCHSIA_(t1->vk_device_, &info, &handle[i]);
    if (result != VK_SUCCESS) {
      PRINT_STDERR("vkGetSemaphoreZirconHandleFUCHSIA_ returned %d", result);
      return false;
    }
  }

  // Import semaphores
  for (uint32_t i = 0; i < kSemaphoreCount; i++) {
    uint32_t flags = temporary ? VK_SEMAPHORE_IMPORT_TEMPORARY_BIT_KHR : 0;
    VkImportSemaphoreZirconHandleInfoFUCHSIA import_info = {
        .sType = VK_STRUCTURE_TYPE_TEMP_IMPORT_SEMAPHORE_ZIRCON_HANDLE_INFO_FUCHSIA,
        .pNext = nullptr,
        .flags = flags,
        .semaphore = t2->vk_semaphore_[i],
        .handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_TEMP_ZIRCON_EVENT_BIT_FUCHSIA,
        .handle = handle[i]};

    result = t1->vkImportSemaphoreZirconHandleFUCHSIA_(t2->vk_device_, &import_info);
    if (result != VK_SUCCESS) {
      PRINT_STDERR("vkImportSemaphoreZirconHandleFUCHSIA_ failed: %d", result);
      return false;
    }
  }

  VkSubmitInfo submit_info1 = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                               .signalSemaphoreCount = 1,
                               .pSignalSemaphores = &t1->vk_semaphore_[0]};
  result = vkQueueSubmit(t1->vk_queue_, 1, &submit_info1, VK_NULL_HANDLE);
  if (result != VK_SUCCESS) {
    PRINT_STDERR("vkQueueSubmit failed: %d", result);
    return false;
  }

  VkPipelineStageFlags stage_flags = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
  VkSubmitInfo submit_info2 = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                               .waitSemaphoreCount = 1,
                               .pWaitSemaphores = &t2->vk_semaphore_[0],
                               .pWaitDstStageMask = &stage_flags,
                               .signalSemaphoreCount = 1,
                               .pSignalSemaphores = &t2->vk_semaphore_[1]};
  result = vkQueueSubmit(t2->vk_queue_, 1, &submit_info2, VK_NULL_HANDLE);
  if (result != VK_SUCCESS) {
    PRINT_STDERR("vkQueueSubmit failed: %d", result);
    return false;
  }

  VkSubmitInfo submit_info3 = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                               .waitSemaphoreCount = 1,
                               .pWaitSemaphores = &t1->vk_semaphore_[1],
                               .pWaitDstStageMask = &stage_flags};
  vkQueueSubmit(t1->vk_queue_, 1, &submit_info3, VK_NULL_HANDLE);
  if (result != VK_SUCCESS) {
    PRINT_STDERR("vkQueueSubmit failed: %d", result);
    return false;
  }

  result = vkQueueWaitIdle(t1->vk_queue_);
  if (result != VK_SUCCESS) {
    PRINT_STDERR("vkQueueWaitIdle failed: %d", result);
    return false;
  }
  result = vkQueueWaitIdle(t2->vk_queue_);
  if (result != VK_SUCCESS) {
    PRINT_STDERR("vkQueueWaitIdle failed: %d", result);
    return false;
  }

  // Destroy semaphores
  for (uint32_t i = 0; i < kSemaphoreCount; i++) {
    vkDestroySemaphore(t1->vk_device_, t1->vk_semaphore_[i], nullptr);
    vkDestroySemaphore(t2->vk_device_, t2->vk_semaphore_[i], nullptr);
  }

  return true;
}

TEST(VulkanExtension, ExternalSemaphoreFuchsia) {
  VulkanTest t1, t2;
  ASSERT_TRUE(t1.Initialize());
  ASSERT_TRUE(t2.Initialize());
  EXPECT_TRUE(VulkanTest::Exec(&t1, &t2, false));
}

TEST(VulkanExtension, TemporaryExternalSemaphoreFuchsia) {
  VulkanTest t1, t2;
  ASSERT_TRUE(t1.Initialize());
  ASSERT_TRUE(t2.Initialize());
  EXPECT_TRUE(VulkanTest::Exec(&t1, &t2, true));
}

TEST(VulkanExtension, QueueExternalSemaphoreFuchsia) {
  VulkanTest t1, t2;
  ASSERT_TRUE(t1.Initialize());
  ASSERT_TRUE(t2.Initialize());
  EXPECT_TRUE(VulkanTest::ExecUsingQueue(&t1, &t2, false));
}

TEST(VulkanExtension, QueueTemporaryExternalSemaphoreFuchsia) {
  VulkanTest t1, t2;
  ASSERT_TRUE(t1.Initialize());
  ASSERT_TRUE(t2.Initialize());
  EXPECT_TRUE(VulkanTest::ExecUsingQueue(&t1, &t2, true));
}

}  // namespace
