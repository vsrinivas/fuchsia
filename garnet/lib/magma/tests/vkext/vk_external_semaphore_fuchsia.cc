// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"

#if defined(MAGMA_USE_SHIM)
#include "vulkan_shim.h"
#else
#include <vulkan/vulkan.h>
#endif
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <thread>
#include <vector>

#include "magma_util/dlog.h"
#include "magma_util/macros.h"
#include "platform_semaphore.h"

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
    PFN_vkImportSemaphoreFuchsiaHandleKHR vkImportSemaphoreFuchsiaHandleKHR_;
    PFN_vkGetSemaphoreFuchsiaHandleKHR vkGetSemaphoreFuchsiaHandleKHR_;

    VkPhysicalDevice vk_physical_device_;
    VkDevice vk_device_;
    VkQueue vk_queue_;
    VkImage vk_image_;
    VkDeviceMemory vk_device_memory_;
    VkCommandPool vk_command_pool_;
    VkCommandBuffer vk_command_buffer_;

    static constexpr uint32_t kSemaphoreCount = 2;
    std::vector<VkSemaphore> vk_semaphore_;
};

bool VulkanTest::Initialize()
{
    if (is_initialized_)
        return false;

    if (!InitVulkan())
        return DRETF(false, "failed to initialize Vulkan");

    is_initialized_ = true;

    return true;
}

bool VulkanTest::InitVulkan()
{
    VkResult result;

    uint32_t extension_count;
    result = vkEnumerateInstanceExtensionProperties(nullptr, &extension_count, nullptr);
    if (result != VK_SUCCESS)
        return DRETF(false, "vkEnumerateInstanceExtensionProperties returned %d\n", result);

    std::vector<VkExtensionProperties> extension_properties(extension_count);
    result = vkEnumerateInstanceExtensionProperties(nullptr, &extension_count,
                                                    extension_properties.data());
    if (result != VK_SUCCESS)
        return DRETF(false, "vkEnumerateInstanceExtensionProperties returned %d\n", result);

    std::vector<const char*> instance_extensions{
        VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME,
        VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME};
    std::vector<const char*> device_extensions{VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME,
                                               VK_KHR_EXTERNAL_SEMAPHORE_FUCHSIA_EXTENSION_NAME};

    uint32_t found_count = 0;
    for (auto& prop : extension_properties) {
        DLOG("instance extension name %s version %u", prop.extensionName, prop.specVersion);
        for (uint32_t i = 0; i < instance_extensions.size(); i++) {
            if ((strcmp(prop.extensionName, instance_extensions[i]) == 0))
                found_count++;
        }
    }

    if (found_count != instance_extensions.size())
        return DRETF(false, "failed to find instance extensions");

    std::vector<const char*> layers;
#if !defined(MAGMA_USE_SHIM)
    // Vulkan loader is needed for loading layers.
    layers.push_back("VK_LAYER_LUNARG_standard_validation");
#endif
    VkInstanceCreateInfo create_info{
        VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, // VkStructureType             sType;
        nullptr,                                // const void*                 pNext;
        0,                                      // VkInstanceCreateFlags       flags;
        nullptr,                                // const VkApplicationInfo*    pApplicationInfo;
        static_cast<uint32_t>(layers.size()),   // uint32_t                    enabledLayerCount;
        layers.data(),                          // const char* const*          ppEnabledLayerNames;
        static_cast<uint32_t>(instance_extensions.size()),
        instance_extensions.data(),
    };
    VkAllocationCallbacks* allocation_callbacks = nullptr;
    VkInstance instance;

    if ((result = vkCreateInstance(&create_info, allocation_callbacks, &instance)) != VK_SUCCESS)
        return DRETF(false, "vkCreateInstance failed %d", result);

    DLOG("vkCreateInstance succeeded");

    uint32_t physical_device_count;
    if ((result = vkEnumeratePhysicalDevices(instance, &physical_device_count, nullptr)) !=
        VK_SUCCESS)
        return DRETF(false, "vkEnumeratePhysicalDevices failed %d", result);

    if (physical_device_count < 1)
        return DRETF(false, "unexpected physical_device_count %d", physical_device_count);

    DLOG("vkEnumeratePhysicalDevices returned count %d", physical_device_count);

    std::vector<VkPhysicalDevice> physical_devices(physical_device_count);
    if ((result = vkEnumeratePhysicalDevices(instance, &physical_device_count,
                                             physical_devices.data())) != VK_SUCCESS)
        return DRETF(false, "vkEnumeratePhysicalDevices failed %d", result);

    for (auto device : physical_devices) {
        VkPhysicalDeviceProperties properties;
        vkGetPhysicalDeviceProperties(device, &properties);
        DLOG("PHYSICAL DEVICE: %s", properties.deviceName);
        DLOG("apiVersion 0x%x", properties.apiVersion);
        DLOG("driverVersion 0x%x", properties.driverVersion);
        DLOG("vendorID 0x%x", properties.vendorID);
        DLOG("deviceID 0x%x", properties.deviceID);
        DLOG("deviceType 0x%x", properties.deviceType);
    }

    uint32_t queue_family_count;
    vkGetPhysicalDeviceQueueFamilyProperties(physical_devices[0], &queue_family_count, nullptr);

    if (queue_family_count < 1)
        return DRETF(false, "invalid queue_family_count %d", queue_family_count);

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

    if (queue_family_index < 0)
        return DRETF(false, "couldn't find an appropriate queue");

    result = vkEnumerateDeviceExtensionProperties(physical_devices[0], nullptr, &extension_count,
                                                  nullptr);
    if (result != VK_SUCCESS)
        return DRETF(false, "vkEnumerateDeviceExtensionProperties returned %d\n", result);

    extension_properties.resize(extension_count);
    result = vkEnumerateDeviceExtensionProperties(physical_devices[0], nullptr, &extension_count,
                                                  extension_properties.data());
    if (result != VK_SUCCESS)
        return DRETF(false, "vkEnumerateDeviceExtensionProperties returned %d\n", result);

    found_count = 0;
    for (auto& prop : extension_properties) {
        DLOG("device extension name %s version %u", prop.extensionName, prop.specVersion);
        for (uint32_t i = 0; i < device_extensions.size(); i++) {
            if ((strcmp(prop.extensionName, device_extensions[i]) == 0))
                found_count++;
        }
    }

    if (found_count != device_extensions.size())
        return DRETF(false, "failed to find device extensions");

    // Create the device
    float queue_priorities[1] = {0.0};

    VkDeviceQueueCreateInfo queue_create_info = {.sType =
                                                     VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                                                 .pNext = nullptr,
                                                 .flags = 0,
                                                 .queueFamilyIndex = 0,
                                                 .queueCount = 1,
                                                 .pQueuePriorities = queue_priorities};
    VkDeviceCreateInfo createInfo = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
                                     .pNext = nullptr,
                                     .flags = 0,
                                     .queueCreateInfoCount = 1,
                                     .pQueueCreateInfos = &queue_create_info,
                                     .enabledLayerCount = 0,
                                     .ppEnabledLayerNames = nullptr,
                                     .enabledExtensionCount =
                                         static_cast<uint32_t>(device_extensions.size()),
                                     .ppEnabledExtensionNames = device_extensions.data(),
                                     .pEnabledFeatures = nullptr};
    VkDevice vkdevice;

    if ((result = vkCreateDevice(physical_devices[0], &createInfo,
                                 nullptr /* allocationcallbacks */, &vkdevice)) != VK_SUCCESS)
        return DRETF(false, "vkCreateDevice failed: %d", result);

    vk_physical_device_ = physical_devices[0];
    vk_device_ = vkdevice;

    vkGetDeviceQueue(vkdevice, queue_family_index, 0, &vk_queue_);

    // Get extension function pointers
    vkGetPhysicalDeviceExternalSemaphorePropertiesKHR_ =
        reinterpret_cast<PFN_vkGetPhysicalDeviceExternalSemaphorePropertiesKHR>(
            vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceExternalSemaphorePropertiesKHR"));
    if (!vkGetPhysicalDeviceExternalSemaphorePropertiesKHR_)
        return DRETF(false, "couldn't find vkGetPhysicalDeviceExternalSemaphorePropertiesKHR");

    vkImportSemaphoreFuchsiaHandleKHR_ = reinterpret_cast<PFN_vkImportSemaphoreFuchsiaHandleKHR>(
        vkGetDeviceProcAddr(vk_device_, "vkImportSemaphoreFuchsiaHandleKHR"));
    if (!vkImportSemaphoreFuchsiaHandleKHR_)
        return DRETF(false, "couldn't find vkImportSemaphoreFuchsiaHandleKHR");

    vkGetSemaphoreFuchsiaHandleKHR_ = reinterpret_cast<PFN_vkGetSemaphoreFuchsiaHandleKHR>(
        vkGetDeviceProcAddr(vk_device_, "vkGetSemaphoreFuchsiaHandleKHR"));
    if (!vkGetSemaphoreFuchsiaHandleKHR_)
        return DRETF(false, "couldn't find vkGetSemaphoreFuchsiaHandleKHR_");

    VkExternalSemaphorePropertiesKHR external_semaphore_properties = {
        .sType = VK_STRUCTURE_TYPE_EXTERNAL_SEMAPHORE_PROPERTIES_KHR,
    };
    VkPhysicalDeviceExternalSemaphoreInfoKHR external_semaphore_info = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_SEMAPHORE_INFO_KHR,
        .pNext = nullptr,
        .handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_FUCHSIA_FENCE_BIT_KHR,
    };
    vkGetPhysicalDeviceExternalSemaphorePropertiesKHR_(
        vk_physical_device_, &external_semaphore_info, &external_semaphore_properties);

    EXPECT_EQ(external_semaphore_properties.compatibleHandleTypes,
              VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_FUCHSIA_FENCE_BIT_KHR);
    EXPECT_EQ(external_semaphore_properties.externalSemaphoreFeatures,
              0u | VK_EXTERNAL_SEMAPHORE_FEATURE_EXPORTABLE_BIT_KHR |
                  VK_EXTERNAL_SEMAPHORE_FEATURE_IMPORTABLE_BIT_KHR);

    // Create semaphores for export
    for (uint32_t i = 0; i < kSemaphoreCount; i++) {
        VkExportSemaphoreCreateInfoKHR export_create_info = {
            .sType = VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO_KHR,
            .pNext = nullptr,
            .handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_FUCHSIA_FENCE_BIT_KHR,
        };

        VkSemaphoreCreateInfo create_info = {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
            .pNext = &export_create_info,
            .flags = 0,
        };

        VkSemaphore semaphore;
        result = vkCreateSemaphore(vk_device_, &create_info, nullptr, &semaphore);
        if (result != VK_SUCCESS)
            return DRETF(false, "vkCreateSemaphore returned %d", result);

        vk_semaphore_.push_back(semaphore);
    }

    return true;
}

bool VulkanTest::Exec(VulkanTest* t1, VulkanTest* t2, bool temporary)
{
    VkResult result;

    std::vector<uint32_t> handle(kSemaphoreCount);

    // Export semaphores
    for (uint32_t i = 0; i < kSemaphoreCount; i++) {
        VkSemaphoreGetFuchsiaHandleInfoKHR info{
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_GET_FUCHSIA_HANDLE_INFO_KHR,
            .pNext = nullptr,
            .semaphore = t1->vk_semaphore_[i],
            .handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_FUCHSIA_FENCE_BIT_KHR,
        };
        result = t1->vkGetSemaphoreFuchsiaHandleKHR_(t1->vk_device_, &info, &handle[i]);
        if (result != VK_SUCCESS)
            return DRETF(false, "vkGetSemaphoreFdKHR returned %d", result);
    }

    std::vector<std::unique_ptr<magma::PlatformSemaphore>> exported(kSemaphoreCount);

    // Import semaphores
    for (uint32_t i = 0; i < kSemaphoreCount; i++) {
        uint32_t flags = temporary ? VK_SEMAPHORE_IMPORT_TEMPORARY_BIT_KHR : 0;
        exported[i] = magma::PlatformSemaphore::Import(handle[i]);
        uint32_t import_handle;
        EXPECT_TRUE(exported[i]->duplicate_handle(&import_handle));
        VkImportSemaphoreFuchsiaHandleInfoKHR import_info = {
            .sType = VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_FUCHSIA_HANDLE_INFO_KHR,
            .pNext = nullptr,
            .flags = flags,
            .semaphore = t2->vk_semaphore_[i],
            .handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_FUCHSIA_FENCE_BIT_KHR,
            .handle = import_handle};

        result = t1->vkImportSemaphoreFuchsiaHandleKHR_(t2->vk_device_, &import_info);
        if (result != VK_SUCCESS)
            return DRETF(false, "vkImportSemaphoreFdKHR failed: %d", result);
    }

    // Test semaphores
    for (uint32_t i = 0; i < kSemaphoreCount; i++) {
        auto& platform_semaphore_export = exported[i];

        // Export the imported semaphores
        VkSemaphoreGetFuchsiaHandleInfoKHR info{
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_GET_FUCHSIA_HANDLE_INFO_KHR,
            .pNext = nullptr,
            .semaphore = t2->vk_semaphore_[i],
            .handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_FUCHSIA_FENCE_BIT_KHR,
        };
        result = t1->vkGetSemaphoreFuchsiaHandleKHR_(t2->vk_device_, &info, &handle[i]);
        if (result != VK_SUCCESS)
            return DRETF(false, "vkGetSemaphoreFdKHR returned %d", result);

        std::shared_ptr<magma::PlatformSemaphore> platform_semaphore_import =
            magma::PlatformSemaphore::Import(handle[i]);

        EXPECT_EQ(platform_semaphore_export->id(), platform_semaphore_import->id());
        DLOG("Testing semaphore %u: 0x%lx", i, platform_semaphore_export->id());

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

bool VulkanTest::ExecUsingQueue(VulkanTest* t1, VulkanTest* t2, bool temporary)
{
    VkResult result;

    std::vector<uint32_t> handle(kSemaphoreCount);

    // Export semaphores
    for (uint32_t i = 0; i < kSemaphoreCount; i++) {
        VkSemaphoreGetFuchsiaHandleInfoKHR info{
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_GET_FUCHSIA_HANDLE_INFO_KHR,
            .pNext = nullptr,
            .semaphore = t1->vk_semaphore_[i],
            .handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_FUCHSIA_FENCE_BIT_KHR,
        };
        result = t1->vkGetSemaphoreFuchsiaHandleKHR_(t1->vk_device_, &info, &handle[i]);
        if (result != VK_SUCCESS)
            return DRETF(false, "vkGetSemaphoreFdKHR returned %d", result);
    }

    // Import semaphores
    for (uint32_t i = 0; i < kSemaphoreCount; i++) {
        uint32_t flags = temporary ? VK_SEMAPHORE_IMPORT_TEMPORARY_BIT_KHR : 0;
        VkImportSemaphoreFuchsiaHandleInfoKHR import_info = {
            .sType = VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_FUCHSIA_HANDLE_INFO_KHR,
            .pNext = nullptr,
            .flags = flags,
            .semaphore = t2->vk_semaphore_[i],
            .handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_FUCHSIA_FENCE_BIT_KHR,
            .handle = handle[i]};

        result = t1->vkImportSemaphoreFuchsiaHandleKHR_(t2->vk_device_, &import_info);
        if (result != VK_SUCCESS)
            return DRETF(false, "vkImportSemaphoreFdKHR failed: %d", result);
    }

    VkSubmitInfo submit_info1 = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                                 .signalSemaphoreCount = 1,
                                 .pSignalSemaphores = &t1->vk_semaphore_[0]};
    result = vkQueueSubmit(t1->vk_queue_, 1, &submit_info1, VK_NULL_HANDLE);
    if (result != VK_SUCCESS)
        return DRETF(false, "vkQueueSubmit failed: %d", result);

    VkPipelineStageFlags stage_flags = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkSubmitInfo submit_info2 = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                                 .waitSemaphoreCount = 1,
                                 .pWaitSemaphores = &t2->vk_semaphore_[0],
                                 .pWaitDstStageMask = &stage_flags,
                                 .signalSemaphoreCount = 1,
                                 .pSignalSemaphores = &t2->vk_semaphore_[1]};
    result = vkQueueSubmit(t2->vk_queue_, 1, &submit_info2, VK_NULL_HANDLE);
    if (result != VK_SUCCESS)
        return DRETF(false, "vkQueueSubmit failed: %d", result);

    VkSubmitInfo submit_info3 = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                                 .waitSemaphoreCount = 1,
                                 .pWaitSemaphores = &t1->vk_semaphore_[1],
                                 .pWaitDstStageMask = &stage_flags};
    vkQueueSubmit(t1->vk_queue_, 1, &submit_info3, VK_NULL_HANDLE);
    if (result != VK_SUCCESS)
        return DRETF(false, "vkQueueSubmit failed: %d", result);

    result = vkQueueWaitIdle(t1->vk_queue_);
    if (result != VK_SUCCESS)
        return DRETF(false, "vkQueueWaitIdle failed: %d", result);
    result = vkQueueWaitIdle(t2->vk_queue_);
    if (result != VK_SUCCESS)
        return DRETF(false, "vkQueueWaitIdle failed: %d", result);

    // Destroy semaphores
    for (uint32_t i = 0; i < kSemaphoreCount; i++) {
        vkDestroySemaphore(t1->vk_device_, t1->vk_semaphore_[i], nullptr);
        vkDestroySemaphore(t2->vk_device_, t2->vk_semaphore_[i], nullptr);
    }

    return true;
}

TEST(VulkanExtension, ExternalSemaphoreFuchsia)
{
    VulkanTest t1, t2;
    ASSERT_TRUE(t1.Initialize());
    ASSERT_TRUE(t2.Initialize());
    ASSERT_TRUE(VulkanTest::Exec(&t1, &t2, false));
}

TEST(VulkanExtension, TemporaryExternalSemaphoreFuchsia)
{
    VulkanTest t1, t2;
    ASSERT_TRUE(t1.Initialize());
    ASSERT_TRUE(t2.Initialize());
    ASSERT_TRUE(VulkanTest::Exec(&t1, &t2, true));
}

TEST(VulkanExtension, QueueExternalSemaphoreFuchsia)
{
    VulkanTest t1, t2;
    ASSERT_TRUE(t1.Initialize());
    ASSERT_TRUE(t2.Initialize());
    ASSERT_TRUE(VulkanTest::ExecUsingQueue(&t1, &t2, false));
}

TEST(VulkanExtension, QueueTemporaryExternalSemaphoreFuchsia)
{
    VulkanTest t1, t2;
    ASSERT_TRUE(t1.Initialize());
    ASSERT_TRUE(t2.Initialize());
    ASSERT_TRUE(VulkanTest::ExecUsingQueue(&t1, &t2, true));
}

} // namespace
